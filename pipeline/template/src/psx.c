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
} polygons[2][8192];
static unsigned long tim_buffer[34000];
static TILE hud_tiles[2][160];
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
    DrawSync(0);psx_frame_discard(&frames);rendering=0;snapshot_valid=0;
    slot=1-frames.front;
    PutDrawEnv(&drawing[slot]);
    if(font_enabled) FntFlush(0); /* Discard text queued by a canceled frame. */
    DrawSync(0);PutDrawEnv(&drawing[slot]);
    if(font_enabled) {
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
    int a;
    int b;
    long x;
    long y;
    long c;
    long s;
    for(k=0;k<3;k++) p[k]=fixed_mul(local[k],object->scale[k]);
    if(object->kind==PSX_OBJECT_BILLBOARD) {
        x=p[0];y=p[1];
        p[0]=fixed_mul(x,camera_cy)-fixed_mul(fixed_mul(camera_sp,camera_sy),y);
        p[1]=fixed_mul(y,camera_cp);
        p[2]=-fixed_mul(x,camera_sy)-fixed_mul(fixed_mul(camera_sp,camera_cy),y);
    } else for(k=0;k<3;k++) {
        if(!object->rotation[k]) continue;
        a=(k+1)%3;b=(k+2)%3;c=object_cos[k];s=object_sin[k];x=p[a];y=p[b];
        p[a]=fixed_mul(c,x)-fixed_mul(s,y);p[b]=fixed_mul(s,x)+fixed_mul(c,y);
    }
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
    long view[3];
    psx_camera_rotation_fixed(camera_cy,camera_sy,camera_cp,camera_sp,matrix.m);
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
    if(polygon_count>=8192) return;
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
            if(shape_dirty) {
                transform(object,object->mesh->vertices[i].p,world);
                for(k=0;k<3;k++) world_vertices[slot][k]=world[k]-object->position[k];
            }
            project_offset(object,world_vertices[slot],projected);
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
    if(!object->alive || object->kind==PSX_OBJECT_CAMERA || index>=STUDIO_OBJECT_LIMIT) return;
    /* Snapshot camera angles persist across objects and render yields. */
    if(!camera_angles_valid || cached_yaw!=yaw()) {
        cached_yaw=yaw();camera_cy=rcos(cached_yaw);camera_sy=rsin(cached_yaw);
    }
    if(!camera_angles_valid || cached_pitch!=pitch()) {
        cached_pitch=pitch();camera_cp=rcos(cached_pitch);camera_sp=rsin(cached_pitch);
    }
    camera_angles_valid=1;
    for(k=0;k<3;k++) if(object->rotation[k]) {
        object_cos[k]=rcos(object->rotation[k]);object_sin[k]=rsin(object->rotation[k]);
    }
    if(object->mesh) draw_mesh(object,index);else draw_box(object,index,0);
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
/* Tiny HUD silhouettes and 3x5 ammo digits use flat GPU tiles, no texture page.
 * All positions are integer screen pixels; allocations are double-buffered. */
static void hud_rect(int *next,int x,int y,int w,int h,int r,int g,int b)
{
    TILE *tile;
    if(*next>=160) return;
    tile=&hud_tiles[buffer_index][(*next)++];setTile(tile);
    setXY0(tile,x,y);setWH(tile,w,h);setRGB0(tile,r,g,b);
    addPrim(&ordering[buffer_index][0],tile);
}
/* Hand-authored pixel silhouettes; horizontal color runs share GPU tiles. */
static void hud_icon(int *next,const char *const *rows,int width,int x,int y)
{
    int row,column,end,r,g,b;
    char pixel;
    for(row=0;row<12;row++) for(column=0;column<width;column=end) {
        pixel=rows[row][column];end=column+1;
        while(end<width && rows[row][end]==pixel) end++;
        if(pixel==' ') continue;
        r=24;g=30;b=38;
        if(pixel=='d') {r=65;g=78;b=88;}
        if(pixel=='m') {r=125;g=145;b=160;}
        if(pixel=='l') {r=190;g=210;b=220;}
        if(pixel=='w') {r=240;g=245;b=245;}
        if(pixel=='b') {r=110;g=82;b=55;}
        hud_rect(next,x+column,y+row,end-column,1,r,g,b);
    }
}
static void hud_weapons(void)
{
    static const unsigned short digits[10]={31599,9362,29671,29391,23497,31183,31215,29257,31727,31695};
    static const char *const pistol[12]={
        "  oooooooooooooooo  ",
        " omllllllllllllmmo  ",
        " ommmmmmmmmmmmmddo  ",
        " oddddddddddddooo   ",
        "  oodddooooooo      ",
        "   oddoo  odo       ",
        "   oddo   odo       ",
        "  odddoooooo        ",
        "  odddo             ",
        " oddddo             ",
        " oddddo             ",
        " oooooo             "
    };
    int next,ammo,digit,column,row,n,x,width;
    next=4;ammo=current_scene->hud.ammo;
    if(ammo<0) ammo=0;if(ammo>99) ammo=99;
    hud_icon(&next,pistol,20,108,7);
    x=103;width=41;
    hud_rect(&next,x,4,width,1,70,225,220);
    hud_rect(&next,x,22,width,1,70,225,220);
    hud_rect(&next,x,5,1,17,70,225,220);
    hud_rect(&next,x+width-1,5,1,17,70,225,220);
    for(n=0;n<2;n++) {
        digit=n?ammo%10:ammo/10;
        for(row=0;row<5;row++) for(column=0;column<3;column++)
            if(digits[digit]&(1U<<(14-row*3-column)))
                hud_rect(&next,131+n*4+column,10+row,1,1,230,235,230);
    }
}
/* Opaque, bordered dialog balloon. Font cells are 8x8 at screen (8,8),
 * with a 38-column / 28-row viewport. Derive the panel from visible text. */
static void hud_dialog(const char *message)
{
    int column,row,left,right,top,bottom,x,y,w,h,next,i;
    unsigned char c;
    if(!message) return;
    column=row=right=bottom=0;left=38;top=28;
    for(i=0;message[i] && i<512;i++) {
        c=(unsigned char)message[i];
        if(c=='\n') {column=0;row++;continue;}
        if(column>=38) {column=0;row++;}
        if(row>=28) break;
        if(c!=' ' && c!='\r' && c!='\t') {
            if(column<left) left=column;if(column>right) right=column;
            if(row<top) top=row;if(row>bottom) bottom=row;
        }
        column++;
    }
    if(left>right || top>bottom) return;
    x=left*8;y=top*8;w=(right-left+3)*8;h=(bottom-top+3)*8;
    if(x+w>316) w=316-x;if(y+h>236) h=236-y;
    next=0;
    /* Reverse OT order: shadow, light border, dark fill, then FntFlush text. */
    hud_rect(&next,x+2,y+2,w-4,h-4,18,25,34);
    hud_rect(&next,x,y,w,h,168,188,202);
    hud_rect(&next,x+3,y+3,w,h,4,8,12);
}
void psDrawHud(void)
{
    TILE *tile;
    int health,maximum,width,i,combat;
    const int x[4]={0,28,30,30},y[4]={0,6,8,8};
    const int w[4]={320,98,94,94},h[4]={27,12,8,8};
    if(!current_scene || !current_scene->hud.enabled) return;
    if(current_scene->hud.style==PSX_HUD_MENU) {hud_dialog(current_scene->hud.message);return;}
    combat=current_scene->hud.style==PSX_HUD_COMBAT;
    maximum=current_scene->hud.max_health;
    if(maximum<1) maximum=1;if(maximum>32767) maximum=32767;
    health=current_scene->hud.health;
    if(health<0) health=0;if(health>maximum) health=maximum;
    width=(int)((long)health*(combat?72:94)/maximum);
    /* OT insertion is reversed: panel, border, empty bar, current HP. */
    for(i=3;i>=(current_scene->hud.style==PSX_HUD_HEALTH_ONLY || current_scene->hud.style==PSX_HUD_COMBAT?1:0);i--) {
        tile=&hud_tiles[buffer_index][i];setTile(tile);
        setXY0(tile,combat?10:x[i],combat?(i==1?8:10):y[i]);
        if(combat && i!=1) setXY0(tile,12,10);
        setWH(tile,i==3?width:combat?(i==1?76:72):w[i],combat?(i==1?9:5):h[i]);
        if(i==0) {setRGB0(tile,12,18,24);}
        else if(i==1) {setRGB0(tile,160,180,185);}
        else if(i==2) {setRGB0(tile,65,20,25);}
        else if(health*4<=maximum) {setRGB0(tile,240,60,60);}
        else {setRGB0(tile,65,220,105);}
        addPrim(&ordering[buffer_index][0],tile);
    }
    if(combat) hud_weapons();
}
void psDisplay(void)
{
    rendering=0;frames.queued=buffer_index;poll_render();
}

/* Keep the DOS compiler command below its command-tail limit. The standalone
 * backend is also compiled against a mock SDK by the host sound tests. */
#include "audio.c"
