#include "../runtime/render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void precise_parts(void)
{
    PsxObject object;
    long local[3]={3,0,0},out[3];
    long cosine[3]={4096,4096,2896},sine[3]={0,0,2896};
    int k;
    memset(&object,0,sizeof(object));
    for(k=0;k<3;k++) object.scale[k]=2048;
    object.rotation[2]=512;
    psx_transform_mesh(&object,local,cosine,sine,out);
    assert(out[0]==1 && out[1]==1 && out[2]==0);
    local[0]=-3;
    psx_transform_mesh(&object,local,cosine,sine,out);
    assert(out[0]==-1 && out[1]==-1 && out[2]==0);
    /* Identity and a large-coordinate fallback preserve their dimensions. */
    local[0]=40000;object.rotation[2]=1024;cosine[2]=0;sine[2]=4096;
    psx_transform_mesh(&object,local,cosine,sine,out);
    assert(out[0]==0 && out[1]==20000 && out[2]==0);
    object.rotation[2]=0;
    psx_transform_mesh(&object,local,cosine,sine,out);
    assert(out[0]==20000 && out[1]==0 && out[2]==0);
}

static void terrain_sharing(void)
{
    PsxVertex vertices[288];
    PsxMesh mesh;
    unsigned short shared[576],table[PSX_POSITION_HASH_SIZE];
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
    assert(psx_mesh_radius(&mesh)==420);
    for(n=0;n<288;n++) for(k=0;k<3;k++) assert(vertices[shared[n]].p[k]==vertices[n].p[k]);
    psx_copy_positions(shared,288,0,288);
    for(n=0;n<288;n++) assert(shared[288+n]==288+shared[n]);
    psx_copy_positions(shared,0,288,288);
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

static void direct_matrices(void)
{
    PsxObject object;
    PsxScene scene;
    PsxProjectionCache cache;
    PsxObjectRotation *rotation;
    short camera[9]={4096,0,0,0,4096,0,0,0,4096},combined[9];
    long local[3]={12,-16,24},out[3];
    int k;
    memset(&scene,0,sizeof(scene));memset(&object,0,sizeof(object));memset(&cache,0,sizeof(cache));
    object.scale[0]=object.scale[1]=object.scale[2]=2048;object.render_flags=PSX_RENDER_CULL_BACKFACES;
    rotation=psx_object_rotation(&scene,&object);assert(rotation && scene.rotation_count==1);
    assert(object.render_flags&PSX_RENDER_CULL_BACKFACES);
    assert(psx_object_rotation(&scene,&object)==rotation && scene.rotation_count==1);
    assert(psx_object_rotation_find(&scene,&object)==rotation->m);
    rotation->m[0]=0;rotation->m[2]=4096;rotation->m[6]=-4096;rotation->m[8]=0;
    psx_transform_object_matrix(&object,local,rotation->m,out);
    assert(out[0]==12 && out[1]==-8 && out[2]==-6);
    assert(psx_gte_object_matrix(camera,rotation->m,object.scale,combined));
    assert(combined[2]==2048 && combined[4]==2048 && combined[6]==-2048);
    psx_projection_snapshot(&cache,&object,&scene);assert(!psx_shape_changed(&cache,&object,&scene));
    rotation->m[1]=32;assert(psx_shape_changed(&cache,&object,&scene));
    psx_projection_snapshot(&cache,&object,&scene);object.position[0]=9;
    assert(!psx_shape_changed(&cache,&object,&scene) && psx_projection_changed(&cache,&object,&scene));
    object.render_flags&=PSX_RENDER_FLAGS_MASK;assert(psx_shape_changed(&cache,&object,&scene));
    for(k=0;k<3;k++) object.scale[k]=131072L;
    assert(!psx_gte_object_matrix(camera,rotation->m,object.scale,combined));
    scene.rotation_count=PSX_OBJECT_ROTATION_CAPACITY;assert(!psx_object_rotation(&scene,&object));
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
    /* Camera-space depth layers bracket ordinary actors using the same OT bins. */
    assert(psx_depth_bucket(480,512)<psx_depth_bucket(570,512));
    assert(psx_depth_bucket(720,512)>psx_depth_bucket(570,512));
    assert(psx_depth_bucket(16,512)==2 && psx_depth_bucket(32767,512)==509);
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
    precise_parts();terrain_sharing();full_hash();direct_matrices();
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
