#include "psx.h"
#include "camera.h"
#include "fps.h"
#include "timing.h"
#include "frame.h"
#include "render.h"
#include <libcd.h>
#include <libapi.h>
#include <stdio.h>
#include <string.h>

/* PAL double buffering derived from the original psx.c; strict C89. */
static DISPENV display[2];
static DRAWENV drawing[2];
static unsigned long ordering[2][STUDIO_DRAW_LIMIT];
static LINE_F2 lines[2][STUDIO_OBJECT_LIMIT][24];
static union {
    POLY_FT3 textured;
    struct { POLY_F3 primitive; DR_TPAGE mode; } flat;
} polygons[2][STUDIO_POLYGON_LIMIT];
static unsigned long tim_buffer[34000];
static TILE overlay_tiles[2][PSX_OVERLAY_CAPACITY];
static POLY_FT4 overlay_quads[2][PSX_OVERLAY_SPRITES];
static struct { SPRT sprite; DR_TPAGE mode; } background_tiles[2][2],depth_tiles[2][PSX_MAX_BACKDROPS];
static PsxProjectionCache cache[STUDIO_OBJECT_LIMIT];
static unsigned short shared_vertices[12288];
static unsigned short position_hash[PSX_POSITION_HASH_SIZE];
static long world_vertices[12288][3]; /* Rotated/scaled offsets, without translation. */
static long projected_vertices[12288][3];
static int buffer_index;
static unsigned int polygon_count;
static PsxScene *current_scene;
static PsxScene *simulation_scene;
static PsxScene frame_scene;
static PsxObject frame_objects[STUDIO_OBJECT_LIMIT];
static PsxFrameQueue frames;
static unsigned long sampled_frames;
static int input_wait;
static int snapshot_valid;
static int rendering;
static unsigned long snapshot_tick;
static PsxTexture *textures;
static unsigned int texture_count;
static int font_enabled = 1;
static PsxFrameRate frame_rate;
static PsxStepClock simulation_clock;
static long camera_cy,camera_sy,camera_cp,camera_sp;
static long object_cos[3],object_sin[3];
static const short *object_matrix;
static int object_gte_direct;
static int cached_yaw,cached_pitch,camera_angles_valid;
unsigned long studio_pad;
static volatile unsigned char controller_buffers[2][34];
void psCheckpoint(PsxCheckpoint *request);
void psAudioInit(void);
int psAudioSetScene(PsxScene *scene);

/* The interrupt only displays an already completed frame. Game code, GPU
 * submission and FPS arithmetic all stay in the main thread. */
static void display_vblank(void)
{
    int slot;
    slot=frames.ready;
    if(slot<0) return;
    PutDispEnv(&display[slot]);SetDispMask(1);
    psx_frame_presented(&frames,(unsigned long)VSync(-1));
}

static void poll_render(void)
{
    int slot;
    unsigned long count,tick;
    do {count=frames.count;tick=frames.tick;} while(count!=frames.count);
    while(sampled_frames!=count) {
        psx_fps_sample(&frame_rate,tick);
        sampled_frames=(sampled_frames+1)&0xffffffffUL;
    }
    if(frames.gpu>=0 && frames.ready<0 && !DrawSync(1)) psx_frame_gpu_done(&frames);
    slot=psx_frame_submit(&frames);
    if(slot<0) return;
    PutDrawEnv(&drawing[slot]);
    DrawOTag(&ordering[slot][STUDIO_DRAW_LIMIT-1]);
    if(font_enabled) FntFlush(0);
}

void psInit(void)
{
    int i;
    ResetCallback(); ResetGraph(0); InitGeom(); SetDispMask(0);SetVideoMode(MODE_PAL);
    memset((void *)controller_buffers,255,sizeof(controller_buffers));
    InitPAD((char *)controller_buffers[0],34,(char *)controller_buffers[1],34);
    StartPAD();ChangeClearPAD(0);
    SetDefDispEnv(&display[0],0,0,320,256); SetDefDispEnv(&display[1],0,256,320,256);
    SetDefDrawEnv(&drawing[0],0,0,320,256); SetDefDrawEnv(&drawing[1],0,256,320,256);
    for(i=0;i<2;i++) {
        display[i].screen.y=16;display[i].screen.w=256;display[i].screen.h=256;
        drawing[i].isbg=1;setRGB0(&drawing[i],16,20,32);
    }
    psx_fps_init(&frame_rate,(unsigned long)VSync(-1),PSX_SIMULATION_HZ);
    psx_step_clock_reset(&simulation_clock,(unsigned long)VSync(-1));
    psx_frame_init(&frames);sampled_frames=0;simulation_scene=0;input_wait=0;snapshot_valid=0;
    FntLoad(960,256);FntOpen(8,8,304,224,0,512);SetDumpFnt(0);buffer_index=0;
    psAudioInit();
    VSyncCallback(display_vblank);
}

/* Only explicit loading/save boundaries may wait for GPU/VBlank. Publish the
 * message before blocking I/O; no arbitrary delay and no callbacks in interrupts. */
void psShowLoading(const char *message)
{
    int slot;
    DRAWENV loading;
    DrawSync(0);psx_frame_discard(&frames);rendering=0;snapshot_valid=0;
    slot=1-frames.front;loading=drawing[slot];setRGB0(&loading,0,0,0);
    PutDrawEnv(&loading);
    if(font_enabled) FntFlush(0); /* Discard text queued by a canceled frame. */
    DrawSync(0);PutDrawEnv(&loading);
    if(font_enabled && message && *message) {
        FntPrint("\n\n\n\n\n      %s",message);FntFlush(0);
    }
    DrawSync(0);VSync(0);PutDispEnv(&display[slot]);
    frames.front=slot;SetDispMask(1);
}

int psSetScene(PsxScene *scene,PsxTexture *images,unsigned int count)
{
    unsigned int i;
    unsigned int start;
    char path[64];
    CdlFILE file;
    TIM_IMAGE tim;
    input_wait=simulation_scene!=0;simulation_scene=0;
    psx_frame_discard(&frames);
    snapshot_valid=0;rendering=0;
    /* Startup and explicit Scene loads may block for SDK/CD I/O. The game
     * calls this only at a GPU-idle boundary, never from a render yield. */
    /* Drop any debug text belonging to a canceled queued frame. Flush it to
     * the hidden buffer so the new Scene starts with an empty font stream. */
    PutDrawEnv(&drawing[1-frames.front]);
    if(font_enabled) FntFlush(0);
    DrawSync(0);
    sampled_frames=frames.count;
    frame_scene=*scene;frame_scene.objects=frame_objects;current_scene=&frame_scene;
    textures=images;texture_count=count;font_enabled=1;
    if(scene->count>STUDIO_OBJECT_LIMIT) return 0;
    start=0;
    for(i=0;i<scene->count && i<STUDIO_OBJECT_LIMIT;i++) {
        cache[i].valid=0;cache[i].mesh=scene->objects[i].mesh;cache[i].start=start;cache[i].count=scene->objects[i].mesh?scene->objects[i].mesh->count:0;
        if(cache[i].count>12288-start) return 0;
        if(scene->objects[i].mesh) psx_share_positions(scene->objects[i].mesh,shared_vertices,start,position_hash);
        cache[i].radius=scene->objects[i].mesh?psx_mesh_radius(scene->objects[i].mesh):0;
        start+=cache[i].count;
    }
    if(start>12288) return 0;
    if(count && !CdInit()) return 0;
    for(i=0;i<count;i++) {
        sprintf(path,"\\%s;1",images[i].path);
        if(!CdSearchFile(&file,path) || ((file.size+2047)/2048)*2048>sizeof(tim_buffer)) return 0;
        if(!CdControl(CdlSetloc,(unsigned char*)&file.pos,0)) return 0;
        if(!CdRead((file.size+2047)/2048,tim_buffer,CdlModeSpeed) || CdReadSync(0,0)<0) return 0;
        if(OpenTIM(tim_buffer)<0 || !ReadTIM(&tim)) return 0;
        if(tim.mode!=2 || tim.prect->x!=images[i].x || tim.prect->y!=images[i].y ||
           tim.prect->w!=images[i].w || tim.prect->h!=images[i].h) return 0;
        LoadImage(tim.prect,tim.paddr);DrawSync(0);
        images[i].tpage=GetTPage(2,0,images[i].x,images[i].y);
        images[i].u=images[i].x%64;images[i].v=images[i].y%256;
        if(images[i].x<1024 && images[i].x+images[i].w>960 && images[i].y<320 && images[i].y+images[i].h>256)
            font_enabled=0;
    }
    if(!psAudioSetScene(scene)) return 0;
    psx_fps_init(&frame_rate,(unsigned long)VSync(-1),PSX_SIMULATION_HZ);
    psx_step_clock_reset(&simulation_clock,(unsigned long)VSync(-1));
    simulation_scene=scene;
    return 1;
}
int psFrameRate(void) { return frame_rate.fps_tenths; }
int psGpuIdle(void) { return DrawSync(1)==0; }
int psUpdate(void)
{
    unsigned int step,steps,i;
    unsigned long input_tick;
    unsigned char packet[8];
    /* Do not submit an old queued frame after a Scene change was requested. */
    if(simulation_scene && simulation_scene->pending_scene) return 0;
    poll_render();
    if(!simulation_scene) return 0;
    if(simulation_scene->checkpoint.operation && !rendering && psGpuIdle()) {
        psShowLoading(simulation_scene->checkpoint.operation==PSX_CHECKPOINT_SAVE?
            "SAVING GAME...\n\n DO NOT REMOVE MEMORY CARD":"READING MEMORY CARD...");
        psCheckpoint(&simulation_scene->checkpoint);
        psx_step_clock_reset(&simulation_clock,(unsigned long)VSync(-1));
        input_wait=1;return 0;
    }
    steps=psx_step_clock_steps(&simulation_clock,(unsigned long)VSync(-1));
    for(step=0;step<steps;step++) {
        /* BIOS input collection stays in its interrupt handler. Copy a coherent
         * packet in the main thread; game callbacks never run in an interrupt. */
        do {
            input_tick=(unsigned long)VSync(-1);
            for(i=0;i<8;i++) packet[i]=controller_buffers[0][i];
        } while(input_tick!=(unsigned long)VSync(-1));
        studio_pad=psx_input_decode(&simulation_scene->input,packet);
        if(input_wait) {
            if(!studio_pad && !simulation_scene->input.x && !simulation_scene->input.z) input_wait=0;
            studio_pad=0;simulation_scene->input.x=simulation_scene->input.z=0;
        }
        psx_scene_update(simulation_scene,studio_pad);
        if(simulation_scene->pending_scene) break;
    }
    return steps!=0;
}
int psFontEnabled(void) { return font_enabled; }
int psBeginFrame(void)
{
    if(simulation_scene && snapshot_valid && snapshot_tick==simulation_clock.tick) return 0;
    poll_render();buffer_index=psx_frame_build_slot(&frames);
    if(buffer_index<0) return 0;
    if(simulation_scene && simulation_scene->prepare_overlay) simulation_scene->prepare_overlay(simulation_scene);
    if(simulation_scene) psx_frame_snapshot(&frame_scene,frame_objects,simulation_scene);
    snapshot_valid=1;snapshot_tick=simulation_clock.tick;
    rendering=1;polygon_count=0;ClearOTagR(ordering[buffer_index],STUDIO_DRAW_LIMIT);return 1;
}
/* Q12 coefficients: handle identity/zero without multiply or divide. */
static long fixed_mul(long a,long b)
{
    if(!b) return 0;
    if(b==4096) return a;
    if(b==-4096) return -a;
    return (a/4096)*b+(a%4096)*b/4096;
}
static int yaw(void) { return current_scene?current_scene->yaw:359; }
static int pitch(void) { return current_scene?current_scene->pitch:391; }
static void transform(const PsxObject *object,const long *local,long *p)
{
    int k;
    long x;
    long y;
    if(object->kind==PSX_OBJECT_BILLBOARD) {
        for(k=0;k<3;k++) p[k]=fixed_mul(local[k],object->scale[k]);
        x=p[0];y=p[1];
        p[0]=fixed_mul(x,camera_cy)-fixed_mul(fixed_mul(camera_sp,camera_sy),y);
        p[1]=fixed_mul(y,camera_cp);
        p[2]=-fixed_mul(x,camera_sy)-fixed_mul(fixed_mul(camera_sp,camera_cy),y);
    } else if(object_matrix) psx_transform_object_matrix(object,local,object_matrix,p);
    else psx_transform_mesh(object,local,object_cos,object_sin,p);
    for(k=0;k<3;k++) p[k]+=object->position[k];
}
static void project(const long *p,long *v)
{
    long x;
    long y;
    long z;
    long horizontal;
    long depth;
    long vertical;
    x=p[0]-(current_scene?current_scene->camera[0]:0);
    y=p[1]-(current_scene?current_scene->camera[1]:0);
    z=p[2]-(current_scene?current_scene->camera[2]:0);
    horizontal=fixed_mul(x,camera_cy)-fixed_mul(z,camera_sy);
    depth=fixed_mul(x,camera_sy)+fixed_mul(z,camera_cy);
    vertical=fixed_mul(depth,camera_sp)-fixed_mul(y,camera_cp);
    v[0]=horizontal;v[1]=vertical;
    v[2]=(current_scene && current_scene->camera_eye?0:(long)PSX_CAMERA_DISTANCE)-fixed_mul(depth,camera_cp)-fixed_mul(y,camera_sp);
}
/* GTE multiplies Q12 camera rotation by signed 16-bit local offsets and
 * adds a full-width translation. Projection/clipping retain integer precision. */
static void prepare_gte(const PsxObject *object)
{
    MATRIX matrix;
    short combined[9];
    int i;
    long view[3];
    psx_camera_rotation_fixed(camera_cy,camera_sy,camera_cp,camera_sp,matrix.m);
    object_gte_direct=object_matrix && object->kind!=PSX_OBJECT_BILLBOARD &&
        psx_gte_object_matrix(&matrix.m[0][0],object_matrix,object->scale,combined);
    if(object_gte_direct) for(i=0;i<9;i++) matrix.m[i/3][i%3]=combined[i];
    project(object->position,view);
    matrix.t[0]=view[0];matrix.t[1]=view[1];matrix.t[2]=view[2];
    SetRotMatrix(&matrix);SetTransMatrix(&matrix);
}
static void project_offset(const PsxObject *object,const long *offset,long *view)
{
    SVECTOR input;
    VECTOR output;
    long flag,world[3];
    int k;
    if(psx_gte_offset_fits(offset)) {
        input.vx=(short)offset[0];input.vy=(short)offset[1];input.vz=(short)offset[2];input.pad=0;
        RotTrans(&input,&output,&flag);
        if(!((unsigned long)flag&0x80000000UL)) {
            view[0]=output.vx;view[1]=output.vy;view[2]=output.vz;return;
        }
    }
    /* Wide rotated offsets or GTE overflow use the validated integer path. */
    for(k=0;k<3;k++) world[k]=offset[k]+object->position[k];
    project(world,view);
}
/* Articulated vertices already have a complete object-to-camera GTE matrix. */
static void project_local_matrix(const PsxObject *object,const long *local,long *view)
{
    SVECTOR input;
    VECTOR output;
    long flag,world[3];
    if(psx_gte_offset_fits(local)) {
        input.vx=(short)local[0];input.vy=(short)local[1];input.vz=(short)local[2];input.pad=0;
        RotTrans(&input,&output,&flag);
        if(!((unsigned long)flag&0x80000000UL)) {view[0]=output.vx;view[1]=output.vy;view[2]=output.vz;return;}
    }
    transform(object,local,world);project(world,view);
}
static int screen_project(const long *view,long *screen)
{
    long result[3];
    if(!psx_camera_project_fixed(current_scene?current_scene->projection:0,
        current_scene?current_scene->zoom:3686,view,result)) {
        screen[0]=0;screen[1]=0;screen[2]=view[2]-600;return 0;
    }
    screen[0]=160+result[0];screen[1]=128+result[1];screen[2]=result[2];
    return 1;
}
static void emit_triangle(long points[3][3],unsigned char uv[3][2],PsxTexture *texture,int flags)
{
    int bucket;
    POLY_FT3 *ft;
    POLY_F3 *flat;
    DR_TPAGE *mode;
    if(polygon_count>=STUDIO_POLYGON_LIMIT) return;
    bucket=psx_triangle_bucket(points,STUDIO_DRAW_LIMIT,flags);
    if(texture) {
        ft=&polygons[buffer_index][polygon_count++].textured;setPolyFT3(ft);
        if(flags&PSX_RENDER_EMISSIVE) setRGB0(ft,192,192,192);else setRGB0(ft,128,128,128);
        setXY3(ft,(short)points[0][0],(short)points[0][1],(short)points[1][0],(short)points[1][1],(short)points[2][0],(short)points[2][1]);
        setUV3(ft,uv[0][0]+texture->u,uv[0][1]+texture->v,uv[1][0]+texture->u,uv[1][1]+texture->v,uv[2][0]+texture->u,uv[2][1]+texture->v);
        ft->tpage=texture->tpage;addPrim(&ordering[buffer_index][bucket],ft);
    } else {
        flat=&polygons[buffer_index][polygon_count].flat.primitive;
        mode=&polygons[buffer_index][polygon_count++].flat.mode;setPolyF3(flat);setRGB0(flat,128,184,255);
        if(flags&PSX_RENDER_DECAL) {
            if(flags&PSX_RENDER_ALERT) setRGB0(flat,100,48,8);
            else setRGB0(flat,16,64,24);
            setSemiTrans(flat,1);
        }
        setXY3(flat,(short)points[0][0],(short)points[0][1],(short)points[1][0],(short)points[1][1],(short)points[2][0],(short)points[2][1]);
        addPrim(&ordering[buffer_index][bucket],flat);
        if(flags&PSX_RENDER_DECAL) {
            /* Reverse OT insertion: set blending before its own primitive.
             * Fans add only a faint quarter-strength tint, preserving terrain. */
            setDrawTPage(mode,drawing[buffer_index].dfe,drawing[buffer_index].dtd,GetTPage(0,3,0,0));
            addPrim(&ordering[buffer_index][bucket],mode);
        }
    }
}
static void draw_mesh(const PsxObject *object,unsigned int index)
{
    unsigned int i;
    int j;
    int k;
    int count;
    int inside;
    int near_clip;
    long world[3];
    long radius,scale,value;
    long triangle[3][3];
    unsigned char uv[3][2];
    long a[12][5];
    long b[12][5];
    long projected[5];
    PsxProjectionCache *c;
    int shape_dirty;
    int gte_ready;
    unsigned int common,region,edges;
    int has_near;
    unsigned int slot,source,unique;
    PsxTexture *texture;
    c=&cache[index];gte_ready=0;
    if(object->mesh->count>c->count) return;
    shape_dirty=psx_shape_changed(c,object,current_scene);
    if(psx_projection_changed(c,object,current_scene)) {
        if(c->mesh!=object->mesh) {
            /* Immutable poses also exist as hidden scene objects. Reuse their
             * position map; UV corners remain independent. Unknown module
             * meshes retain the hash fallback. No additional console RAM. */
            for(source=0;source<current_scene->count;source++)
                if(source!=index && cache[source].mesh==object->mesh) break;
            if(source<current_scene->count) {
                psx_copy_positions(shared_vertices,c->start,cache[source].start,object->mesh->count);
                c->radius=cache[source].radius;
            } else {
                psx_share_positions(object->mesh,shared_vertices,c->start,position_hash);
                c->radius=psx_mesh_radius(object->mesh);
            }
            c->mesh=object->mesh;c->valid=0;
        }
        if(object->mesh->count>=96) {
            scale=0;
            for(k=0;k<3;k++) {
                value=object->scale[k];if(value<0) value=-value;
                if(value>scale) scale=value;
            }
            radius=fixed_mul(c->radius,scale);
            project(object->position,world);
            if(psx_camera_bounds_outside_fixed(current_scene->projection,current_scene->zoom,world,radius)) {
                /* No projected/world cache was filled for this pose. Keep it
                 * invalid so re-entry transforms every required position. */
                c->valid=0;return;
            }
        }
        prepare_gte(object);gte_ready=1;
        common=15U;has_near=0;unique=0;
        for(i=0;i<object->mesh->count;i++) {
            slot=c->start+i;
            if(shared_vertices[slot]!=slot) continue;
            if(!(unique++&15U) && psUpdate()) {
                if(simulation_scene->pending_scene) return;
                prepare_gte(object);
            }
            if(object_gte_direct) project_local_matrix(object,object->mesh->vertices[i].p,projected);
            else {
                if(shape_dirty || object_matrix) {
                    transform(object,object->mesh->vertices[i].p,world);
                    for(k=0;k<3;k++) world_vertices[slot][k]=world[k]-object->position[k];
                }
                project_offset(object,world_vertices[slot],projected);
            }
            screen_project(projected,projected);
            for(k=0;k<3;k++) projected_vertices[slot][k]=projected[k];
            common&=psx_screen_region(projected);
            if(current_scene->projection && projected[2]<PSX_CAMERA_NEAR-PSX_CAMERA_DISTANCE) has_near=1;
        }
        c->outside=has_near?0:common;
        psx_projection_snapshot(c,object,current_scene);
    }
    if(c->outside) return;
    texture=object->mesh->texture>=0 && (unsigned int)object->mesh->texture<texture_count?&textures[object->mesh->texture]:0;
    for(i=0;i+2<object->mesh->count;i+=3) {
        if(!(i%48U) && psUpdate()) {
            if(simulation_scene->pending_scene) return;
            gte_ready=0;
        }
        inside=1;near_clip=0;common=15U;edges=0;
        for(j=0;j<3;j++) {
            for(k=0;k<3;k++) triangle[j][k]=projected_vertices[shared_vertices[c->start+i+j]][k];
            uv[j][0]=object->mesh->vertices[i+j].uv[0];uv[j][1]=object->mesh->vertices[i+j].uv[1];
            if(current_scene->projection && triangle[j][2]<(long)(PSX_CAMERA_NEAR-PSX_CAMERA_DISTANCE)) near_clip=1;
            region=psx_screen_region(triangle[j]);common&=region;edges|=region;if(region) inside=0;
        }
        if(common && !near_clip) continue;
        if(!near_clip && (object->render_flags&PSX_RENDER_CULL_BACKFACES) &&
            psx_triangle_backface(triangle)) continue;
        if(!near_clip && (inside || psx_gpu_triangle_fits(triangle))) {emit_triangle(triangle,uv,texture,object->render_flags);continue;}
        for(j=0;j<3;j++) {
            for(k=0;k<3;k++) a[j][k]=triangle[j][k];a[j][3]=uv[j][0];a[j][4]=uv[j][1];
        }
        count=3;
        if(near_clip) {
            if(!gte_ready) {prepare_gte(object);gte_ready=1;}
            for(j=0;j<3;j++) {
                project_offset(object,world_vertices[shared_vertices[c->start+i+j]],a[j]);
            }
            count=psx_camera_clip_polygon_fixed(a,3,b,2,(long)PSX_CAMERA_NEAR,1);
            for(j=0;j<count;j++) {
                screen_project(b[j],a[j]);a[j][3]=b[j][3];a[j][4]=b[j][4];
            }
        }
        for(j=0;j<count;j++) for(k=0;k<5;k++) b[j][k]=a[j][k];
        /* A convex clip cannot cross a boundary that all input points satisfy.
         * Near-plane reprojection can change these bounds: retain all four there. */
        if(near_clip) edges=15U;
        for(k=0;k<4;k++) if(edges&(1U<<k)) {
            count=psx_camera_clip_polygon_fixed(b,count,a,k/2,
                k==0 || k==2?0:k==1?319:255,!(k&1));
            for(j=0;j<count;j++) {
                int component;
                for(component=0;component<5;component++) b[j][component]=a[j][component];
            }
        }
        for(j=1;j+1<count;j++) {
            for(k=0;k<3;k++) {triangle[0][k]=(long)b[0][k];triangle[1][k]=(long)b[j][k];triangle[2][k]=(long)b[j+1][k];}
            uv[0][0]=(unsigned char)b[0][3];uv[0][1]=(unsigned char)b[0][4];uv[1][0]=(unsigned char)b[j][3];uv[1][1]=(unsigned char)b[j][4];
            uv[2][0]=(unsigned char)b[j+1][3];uv[2][1]=(unsigned char)b[j+1][4];emit_triangle(triangle,uv,texture,object->render_flags);
        }
    }
}
static int clip_line(long a[5],long b[5])
{
    return psx_camera_clip_line_fixed(a,b,0,0,1) &&
        psx_camera_clip_line_fixed(a,b,0,319,0) &&
        psx_camera_clip_line_fixed(a,b,1,0,1) &&
        psx_camera_clip_line_fixed(a,b,1,255,0);
}
static void draw_box(const PsxObject *object,unsigned int index,int collider)
{
    static const unsigned char edges[12][2]={{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
    long points[8][5];
    long a[12][5];
    long local[3];
    long world[3];
    int vertex;
    int axis;
    int edge;
    LINE_F2 *line;
    for(vertex=0;vertex<8;vertex++) {
        for(axis=0;axis<3;axis++) {
            local[axis]=collider?object->half_size[axis]:24;
            if(!(vertex&(1<<axis))) local[axis]=-local[axis];
        }
        if(collider) for(axis=0;axis<3;axis++) world[axis]=local[axis]+object->position[axis]+object->collider_offset[axis];
        else { if(object->kind!=PSX_OBJECT_MESH) local[2]=0;transform(object,local,world); }
        project(world,points[vertex]);points[vertex][3]=0;points[vertex][4]=0;
    }
    for(edge=0;edge<12;edge++) {
        for(axis=0;axis<5;axis++) {a[0][axis]=points[edges[edge][0]][axis];a[1][axis]=points[edges[edge][1]][axis];}
        if(current_scene && current_scene->projection==PSX_PROJECTION_PERSPECTIVE && !psx_camera_clip_line_fixed(a[0],a[1],2,(long)PSX_CAMERA_NEAR,1)) continue;
        for(vertex=0;vertex<2;vertex++) {
            screen_project(a[vertex],a[vertex]);
        }
        if(!clip_line(a[0],a[1])) continue;
        line=&lines[buffer_index][index][edge+(collider?12:0)];setLineF2(line);
        setXY2(line,(short)a[0][0],(short)a[0][1],(short)a[1][0],(short)a[1][1]);
        if(collider) {setRGB0(line,48,216,96);} else {setRGB0(line,128,184,255);}
        addPrim(&ordering[buffer_index][1],line);
    }
}
void psDrawObject(const PsxObject *object,unsigned int index)
{
    int k;
    if(!object->alive || (object->render_flags&PSX_RENDER_HIDDEN) || object->kind==PSX_OBJECT_CAMERA || index>=STUDIO_OBJECT_LIMIT) return;
    if(!object->mesh && (object->render_flags&PSX_RENDER_NO_FALLBACK) &&
       (!current_scene || !current_scene->show_colliders)) return;
    /* Snapshot camera angles persist across objects and render yields. */
    if(!camera_angles_valid || cached_yaw!=yaw()) {
        cached_yaw=yaw();camera_cy=rcos(cached_yaw);camera_sy=rsin(cached_yaw);
    }
    if(!camera_angles_valid || cached_pitch!=pitch()) {
        cached_pitch=pitch();camera_cp=rcos(cached_pitch);camera_sp=rsin(cached_pitch);
    }
    camera_angles_valid=1;
    object_matrix=psx_object_rotation_find(current_scene,object);
    for(k=0;k<3;k++) if(!object_matrix && object->rotation[k]) {
        object_cos[k]=rcos(object->rotation[k]);object_sin[k]=rsin(object->rotation[k]);
    }
    if(object->mesh) draw_mesh(object,index);
    else if(!(object->render_flags&PSX_RENDER_NO_FALLBACK)) draw_box(object,index,0);
    if(current_scene && current_scene->show_colliders) draw_box(object,index,1);
}
const PsxScene *psDrawScene(void)
{
    unsigned int i;
    for(i=0;i<frame_scene.count;i++) {
        psUpdate();
        if(simulation_scene && simulation_scene->pending_scene) break;
        psDrawObject(&frame_objects[i],i);
    }
    return &frame_scene;
}
/* Generic painter-ordered overlay. All geometry/text belongs to the caller;
 * native packet storage is bounded and independently double-buffered. */
void psDrawOverlay(void)
{
    int i,next;
    const PsxOverlayCommand *command;
    PsxTexture *texture;
    TILE *tile;
    POLY_FT4 *quad;
    int u,v;
    if(!current_scene) return;
    next=0;
    /* Reverse OT insertion preserves application painter order. */
    for(i=(int)current_scene->overlay.count-1;i>=0;i--) {
        if(i>=PSX_OVERLAY_CAPACITY) continue;
        command=&current_scene->overlay.commands[i];
        if(command->type==PSX_OVERLAY_RECT) {
            tile=&overlay_tiles[buffer_index][i];setTile(tile);
            setXY0(tile,command->x,command->y);setWH(tile,command->w,command->h);
            setRGB0(tile,command->r,command->g,command->b);
            addPrim(&ordering[buffer_index][0],tile);
        } else if(command->type==PSX_OVERLAY_SPRITE && next<PSX_OVERLAY_SPRITES && command->texture<texture_count) {
            texture=&textures[command->texture];
            if(command->u+command->source_w>texture->w || command->v+command->source_h>texture->h) continue;
            u=texture->u+command->u;v=texture->v+command->v;
            if(u+command->source_w>256 || v+command->source_h>256) continue;
            quad=&overlay_quads[buffer_index][next++];setPolyFT4(quad);setRGB0(quad,128,128,128);
            setXY4(quad,command->x,command->y,command->x+command->w,command->y,
                command->x,command->y+command->h,command->x+command->w,command->y+command->h);
            setUV4(quad,u,v,u+command->source_w-1,v,u,v+command->source_h-1,
                u+command->source_w-1,v+command->source_h-1);
            quad->tpage=texture->tpage;addPrim(&ordering[buffer_index][0],quad);
        }
    }
    if(psFontEnabled() && current_scene->overlay.text[0]) FntPrint("%s",current_scene->overlay.text);
}
/* Insert at the same depth convention as ordinary triangles. Higher camera
 * depth draws first. Reverse equal-depth layers so list order is compositing order. */
static void draw_depth_layers(void)
{
    int i,bucket;
    PsxBackdrop *layer;
    PsxTexture *texture;
    SPRT *sprite;
    DR_TPAGE *mode;
    if(!current_scene) return;
    for(i=(int)current_scene->backdrop_count-1;i>=0;i--) {
        if(i>=PSX_MAX_BACKDROPS) continue;
        layer=&current_scene->backdrops[i];if(layer->texture>=texture_count) continue;
        texture=&textures[layer->texture];sprite=&depth_tiles[buffer_index][i].sprite;mode=&depth_tiles[buffer_index][i].mode;
        setSprt(sprite);setShadeTex(sprite,1);setRGB0(sprite,128,128,128);
        setXY0(sprite,layer->x,layer->y);setUV0(sprite,texture->u,texture->v);setWH(sprite,layer->w,layer->h);
        setDrawTPage(mode,drawing[buffer_index].dfe,drawing[buffer_index].dtd,texture->tpage);
        bucket=psx_depth_bucket(layer->depth,STUDIO_DRAW_LIMIT);
        addPrim(&ordering[buffer_index][bucket],sprite);addPrim(&ordering[buffer_index][bucket],mode);
    }
}
/* Draw after building the world OT so these primitives are its first entries.
 * Exact-size sprites avoid affine seams and keep the background fixed on screen. */
static void draw_background(void)
{
    unsigned int first,part;
    SPRT *sprite;
    DR_TPAGE *mode;
    PsxTexture *image;
    if(!current_scene || !current_scene->background_texture) return;
    first=current_scene->background_texture-1;
    if(first>=texture_count || texture_count-first<2) return;
    for(part=0;part<2;part++) {
        image=&textures[first+part];sprite=&background_tiles[buffer_index][part].sprite;
        mode=&background_tiles[buffer_index][part].mode;
        setSprt(sprite);setShadeTex(sprite,1);setRGB0(sprite,128,128,128);
        setXY0(sprite,part?256:0,0);setUV0(sprite,image->u,image->v);
        setWH(sprite,part?64:256,256);
        setDrawTPage(mode,0,0,image->tpage);
        addPrim(&ordering[buffer_index][STUDIO_DRAW_LIMIT-1],sprite);
        addPrim(&ordering[buffer_index][STUDIO_DRAW_LIMIT-1],mode);
    }
}
void psDisplay(void)
{
    draw_background();draw_depth_layers();
    rendering=0;frames.queued=buffer_index;poll_render();
}

/* Keep the DOS compiler command below its command-tail limit. The standalone
 * backend is also compiled against a mock SDK by the host sound tests. */
#include "audio.c"
