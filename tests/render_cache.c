#include "../runtime/render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void terrain_sharing(void)
{
    PsxVertex vertices[288];
    PsxMesh mesh;
    unsigned short shared[288],table[PSX_POSITION_HASH_SIZE];
    const int corner[6]={0,1,2,0,2,3};
    const int dx[4]={0,1,1,0},dz[4]={0,0,1,1};
    int x,z,k,n;
    n=0;
    for(z=0;z<6;z++) for(x=0;x<8;x++) for(k=0;k<6;k++) {
        vertices[n].p[0]=-240+(x+dx[corner[k]])*60;
        vertices[n].p[1]=0;vertices[n].p[2]=-180+(z+dz[corner[k]])*60;
        vertices[n].uv[0]=(unsigned char)n;vertices[n].uv[1]=(unsigned char)(255-n);n++;
    }
    mesh.vertices=vertices;mesh.count=288;mesh.texture=0;
    assert(psx_share_positions(&mesh,shared,0,table)==63);
    for(n=0;n<288;n++) for(k=0;k<3;k++) assert(vertices[shared[n]].p[k]==vertices[n].p[k]);
    /* Different positions deliberately collide in the power-of-two hash. */
    mesh.count=3;
    for(n=0;n<3;n++) for(k=0;k<3;k++) vertices[n].p[k]=0;
    vertices[1].p[0]=8192;
    assert(psx_share_positions(&mesh,shared,0,table)==2);
    assert(shared[0]==0 && shared[1]==1 && shared[2]==0);
}

static void full_hash(void)
{
    static PsxVertex vertices[9000];
    static unsigned short shared[9000],table[PSX_POSITION_HASH_SIZE];
    PsxMesh mesh;
    unsigned int i;
    for(i=0;i<9000;i++) vertices[i].p[0]=i;
    mesh.vertices=vertices;mesh.count=9000;mesh.texture=-1;
    assert(psx_share_positions(&mesh,shared,0,table)==9000);
    for(i=0;i<9000;i++) assert(shared[i]==i);
    vertices[8999].p[0]=0;
    assert(psx_share_positions(&mesh,shared,0,table)==8999);
    assert(shared[8999]==0);
}

int main(void)
{
    const PsxVertex vertices[6]={
        {{0,0,0},{0,0}},{{10,0,0},{255,0}},{{10,10,0},{255,255}},
        {{0,0,0},{127,127}},{{10,10,0},{0,0}},{{0,10,0},{0,255}}
    };
    PsxMesh mesh;
    PsxObject object;
    PsxScene scene;
    PsxProjectionCache cache;
    unsigned short shared[32],table[PSX_POSITION_HASH_SIZE];
    long offset[3]={-32768,32767,0};
    long screen[3]={0,0,0};
    long triangle[3][3]={{0,0,-500},{0,0,1500},{0,0,-500}};
    /* A broad ground triangle must stay behind a nearby actor even when
     * average depth would sort it in front. Reserve its bucket exclusively. */
    assert(psx_triangle_bucket(triangle,2048,1)==2047);
    assert(psx_triangle_bucket(triangle,2048,PSX_RENDER_DECAL)==2046);
    assert(psx_triangle_bucket(triangle,2048,0)<2047);
    triangle[0][2]=triangle[1][2]=triangle[2][2]=100000;
    assert(psx_triangle_bucket(triangle,2048,0)==2045);
    triangle[0][2]=triangle[1][2]=triangle[2][2]=-100000;
    assert(psx_triangle_bucket(triangle,2048,0)==2);
    triangle[0][0]=triangle[0][1]=0;
    triangle[1][0]=10;triangle[1][1]=0;
    triangle[2][0]=0;triangle[2][1]=10;
    assert(!psx_triangle_backface(triangle));
    triangle[2][1]=-10;assert(psx_triangle_backface(triangle));
    triangle[2][1]=0;assert(psx_triangle_backface(triangle));
    triangle[2][1]=-100000;assert(!psx_triangle_backface(triangle));
    triangle[0][0]=-400;triangle[0][1]=-200;
    triangle[1][0]=623;triangle[1][1]=311;
    triangle[2][0]=0;triangle[2][1]=0;
    assert(psx_gpu_triangle_fits(triangle));
    triangle[1][0]=624;assert(!psx_gpu_triangle_fits(triangle));
    triangle[1][0]=623;triangle[1][1]=312;assert(!psx_gpu_triangle_fits(triangle));
    triangle[0][1]=triangle[1][1]=triangle[2][1]=767;
    assert(psx_gpu_triangle_fits(triangle));
    triangle[1][1]=768;assert(!psx_gpu_triangle_fits(triangle));
    triangle[1][1]=767;triangle[1][0]=1024;assert(!psx_gpu_triangle_fits(triangle));
    assert(psx_screen_region(screen)==0);screen[0]=319;screen[1]=255;
    assert(psx_screen_region(screen)==0);screen[0]=320;assert(psx_screen_region(screen)==2);
    screen[0]=-1;screen[1]=-1;assert(psx_screen_region(screen)==5);
    screen[0]=100;screen[1]=256;assert(psx_screen_region(screen)==8);
    terrain_sharing();full_hash();
    assert(psx_gte_offset_fits(offset));offset[0]=-32769;assert(!psx_gte_offset_fits(offset));
    offset[0]=0;offset[1]=32768;assert(!psx_gte_offset_fits(offset));
    memset(&object,0,sizeof(object));memset(&scene,0,sizeof(scene));
    memset(&cache,0,sizeof(cache));memset(shared,0,sizeof(shared));
    mesh.vertices=vertices;mesh.count=6;mesh.texture=0;
    assert(psx_share_positions(&mesh,shared,7,table)==4);
    assert(shared[7]==7 && shared[10]==7 && shared[11]==9 && shared[12]==12);
    assert(mesh.vertices[0].uv[0]==0 && mesh.vertices[3].uv[0]==127);
    object.mesh=&mesh;object.scale[0]=object.scale[1]=object.scale[2]=4096;
    scene.zoom=4096;cache.start=7;cache.count=6;
    assert(psx_shape_changed(&cache,&object,&scene));
    assert(psx_projection_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);
    assert(!psx_shape_changed(&cache,&object,&scene));
    assert(!psx_projection_changed(&cache,&object,&scene));
    assert(cache.start==7 && cache.count==6);
    scene.camera[0]=10;
    assert(!psx_shape_changed(&cache,&object,&scene));
    assert(psx_projection_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);object.position[1]=20;
    assert(!psx_shape_changed(&cache,&object,&scene));
    assert(psx_projection_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);scene.yaw=100;
    assert(!psx_shape_changed(&cache,&object,&scene));
    assert(psx_projection_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);object.scale[2]=8192;
    assert(psx_shape_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);object.rotation[0]=300;
    assert(psx_shape_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);object.kind=PSX_OBJECT_BILLBOARD;
    assert(psx_shape_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);scene.pitch=500;
    assert(psx_shape_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);scene.zoom=2048;
    assert(!psx_shape_changed(&cache,&object,&scene));
    assert(psx_projection_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);scene.projection=1;
    assert(psx_projection_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);scene.camera_eye=1;
    assert(psx_projection_changed(&cache,&object,&scene));
    cache.valid=0;assert(psx_shape_changed(&cache,&object,&scene));
    puts("C89 render cache: shared positions/UV seams, geometry and camera invalidation passed.");
    return 0;
}
