#ifndef STUDIO_RENDER_CACHE_H
#define STUDIO_RENDER_CACHE_H
#include "object.h"

/* A mesh keeps independent UVs for every triangle corner. Only its position
 * calculations are shared. Absolute indices fit the renderer's 12288 slots. */
#define PSX_POSITION_HASH_SIZE 8192
static unsigned int psx_share_positions(const PsxMesh *mesh,
    unsigned short *shared,unsigned int start,unsigned short *table)
{
    unsigned int i,j,unique,size;
    unsigned long hash;
    size=1;
    while(size<mesh->count*2 && size<PSX_POSITION_HASH_SIZE) size*=2;
    for(i=0;i<size;i++) table[i]=65535U;
    unique=0;
    for(i=0;i<mesh->count;i++) {
        /* Exported assets fit the hash. Hand-written meshes may fill it;
         * bound probing and retain correctness without a frame-loop allocation. */
        if(unique>=size) {
            for(j=0;j<i;j++) if(mesh->vertices[i].p[0]==mesh->vertices[j].p[0] &&
                mesh->vertices[i].p[1]==mesh->vertices[j].p[1] &&
                mesh->vertices[i].p[2]==mesh->vertices[j].p[2]) break;
            shared[start+i]=(unsigned short)(start+j);
            if(j==i) unique++;
            continue;
        }
        hash=((unsigned long)mesh->vertices[i].p[0]*73856093UL ^
            (unsigned long)mesh->vertices[i].p[1]*19349663UL ^
            (unsigned long)mesh->vertices[i].p[2]*83492791UL)&(size-1);
        while(table[hash]!=65535U) {
            j=table[hash];
            if(mesh->vertices[i].p[0]==mesh->vertices[j].p[0] &&
               mesh->vertices[i].p[1]==mesh->vertices[j].p[1] &&
               mesh->vertices[i].p[2]==mesh->vertices[j].p[2]) break;
            hash=(hash+1)&(size-1);
        }
        if(table[hash]==65535U) {table[hash]=(unsigned short)i;unique++;}
        shared[start+i]=(unsigned short)(start+table[hash]);
    }
    return unique;
}

/* Source/destination are disjoint object allocations, including hidden pose
 * references. Relocate absolute indices without touching position/UV data. */
static void psx_copy_positions(unsigned short *shared,unsigned int destination,
    unsigned int source,unsigned int count)
{
    unsigned int i;
    for(i=0;i<count;i++)
        shared[destination+i]=(unsigned short)(destination+shared[source+i]-source);
}

/* L1 radius bounds Euclidean distance for any subsequent rotation. Meshes
 * are immutable; compute once per asset/pose, not once per rendered frame. */
static long psx_mesh_radius(const PsxMesh *mesh)
{
    unsigned int i;
    int k;
    long radius,sum,value;
    radius=0;
    for(i=0;i<mesh->count;i++) {
        sum=0;
        for(k=0;k<3;k++) {value=mesh->vertices[i].p[k];sum+=value<0?-value:value;}
        if(sum>radius) radius=sum;
    }
    return radius;
}

static int psx_gte_offset_fits(const long *offset)
{
    return offset[0]>=-32768 && offset[0]<=32767 && offset[1]>=-32768 &&
        offset[1]<=32767 && offset[2]>=-32768 && offset[2]<=32767;
}

/* Reserve the last OT bucket for explicitly marked ground/backdrops.
 * Normal geometry retains average-depth ordering in the remaining buckets. */
static int psx_triangle_bucket(long points[3][3],int limit,int flags)
{
    long depth;
    if(flags&PSX_RENDER_BACKGROUND) return limit-1;
    if(flags&PSX_RENDER_DECAL) return limit-2;
    depth=(points[0][2]+points[1][2]+points[2][2])/6+256;
    if(depth<2) return 2;
    if(depth>=limit-2) return limit-3;
    return (int)depth;
}

/* Positive screen-space winding is the visible side of the inward-wound
 * example wall meshes. Bound products for a 32-bit PS1 long; wide projections
 * conservatively retain both faces. Degenerate triangles need no GPU packet. */
static int psx_triangle_backface(long p[3][3])
{
    long ax,ay,bx,by;
    ax=p[1][0]-p[0][0];ay=p[1][1]-p[0][1];
    bx=p[2][0]-p[0][0];by=p[2][1]-p[0][1];
    if(ax < -16383 || ax > 16383 || ay < -16383 || ay > 16383 ||
       bx < -16383 || bx > 16383 || by < -16383 || by > 16383) return 0;
    return ax*by-ay*bx<=0;
}

/* Let DRAWENV clip normal-size primitives on the GPU (Psy-Q libgpu).
 * Both buffers use X offset 0 and Y offset 0/256. Keep coordinates within
 * signed 11 bits after either offset; reject GPU span limits conservatively. */
static int psx_gpu_triangle_fits(long p[3][3])
{
    long min_x,max_x,min_y,max_y;
    int i;
    min_x=max_x=p[0][0];min_y=max_y=p[0][1];
    for(i=0;i<3;i++) {
        if(p[i][0]<-1024 || p[i][0]>1023 || p[i][1]<-1024 || p[i][1]>767) return 0;
        if(p[i][0]<min_x) min_x=p[i][0];
        if(p[i][0]>max_x) max_x=p[i][0];
        if(p[i][1]<min_y) min_y=p[i][1];
        if(p[i][1]>max_y) max_y=p[i][1];
    }
    return max_x-min_x<1024 && max_y-min_y<512;
}

static unsigned int psx_screen_region(const long *point)
{
    unsigned int code;
    code=0;
    if(point[0]<0) code|=1U;else if(point[0]>319) code|=2U;
    if(point[1]<0) code|=4U;else if(point[1]>255) code|=8U;
    return code;
}

typedef struct PsxProjectionCache {
    long position[3],rotation[3],scale[3],camera[3],zoom;
    long radius;
    int yaw,pitch,projection,valid,kind,camera_eye;
    unsigned int start,count,outside;
    const PsxMesh *mesh;
} PsxProjectionCache;

static int psx_shape_changed(const PsxProjectionCache *c,const PsxObject *o,
    const PsxScene *scene)
{
    int k;
    if(!c->valid || c->kind!=o->kind || c->mesh!=o->mesh) return 1;
    for(k=0;k<3;k++) if(c->rotation[k]!=o->rotation[k] || c->scale[k]!=o->scale[k]) return 1;
    return o->kind==PSX_OBJECT_BILLBOARD && (c->yaw!=scene->yaw || c->pitch!=scene->pitch);
}

static int psx_projection_changed(const PsxProjectionCache *c,const PsxObject *o,
    const PsxScene *scene)
{
    int k;
    if(psx_shape_changed(c,o,scene) || c->yaw!=scene->yaw || c->pitch!=scene->pitch ||
        c->zoom!=scene->zoom || c->projection!=scene->projection || c->camera_eye!=scene->camera_eye) return 1;
    for(k=0;k<3;k++) if(c->position[k]!=o->position[k] || c->camera[k]!=scene->camera[k]) return 1;
    return 0;
}

static void psx_projection_snapshot(PsxProjectionCache *c,const PsxObject *o,
    const PsxScene *scene)
{
    int k;
    for(k=0;k<3;k++) {
        c->position[k]=o->position[k];c->rotation[k]=o->rotation[k];
        c->scale[k]=o->scale[k];c->camera[k]=scene->camera[k];
    }
    c->yaw=scene->yaw;c->pitch=scene->pitch;c->projection=scene->projection;
    c->camera_eye=scene->camera_eye;c->zoom=scene->zoom;
    c->kind=o->kind;c->mesh=o->mesh;c->valid=1;
}
#endif
