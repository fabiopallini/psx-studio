#include "../runtime/camera.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void close_to(double a,double b) { assert(fabs(a-b)<0.000001); }
static void fixed_clipping(void)
{
    long input[12][5]={{0,0,16,0,0},{60,0,64,60,0},{0,60,64,0,60}};
    long output[12][5],a[5],b[5];
    double reference[5],ratio;
    int count,i,k;
    count=psx_camera_clip_polygon_fixed(input,3,output,2,32,1);
    assert(count==4 && output[0][4]==20 && output[1][3]==20);
    for(i=0;i<count;i++) assert(output[i][2]>=32);
    count=psx_camera_clip_polygon_fixed(output,count,input,0,25,0);
    for(i=0;i<count;i++) assert(input[i][0]<=25);
    /* Large signed coordinates force overflow-safe interpolation on a
     * 32-bit console. Compare all attributes with the desktop reference. */
    for(i=0;i<200;i++) {
        a[0]=-20000000L-i*123L;b[0]=20000000L+i*31L;
        a[1]=15000001L-i*79L;b[1]=-19000000L+i*53L;
        a[2]=32;a[3]=0;a[4]=255;b[2]=60000;b[3]=255;b[4]=0;
        ratio=(319.0-a[0])/(b[0]-a[0]);
        for(k=0;k<5;k++) reference[k]=a[k]+ratio*(b[k]-a[k]);
        assert(psx_camera_clip_line_fixed(a,b,0,319,1));
        assert(a[0]==319);
        for(k=0;k<5;k++) assert(fabs(a[k]-reference[k])<1.000001);
    }
    a[0]=-4;b[0]=-1;assert(!psx_camera_clip_line_fixed(a,b,0,0,1));
    a[0]=0;b[0]=319;assert(psx_camera_clip_line_fixed(a,b,0,0,1));
}

static void fixed_rotation(void)
{
    const long angles[4][4]={{4096,0,4096,0},{0,4096,4096,0},
        {2896,2896,3547,2048},{-2896,-2896,2896,-2896}};
    short matrix[3][3];
    PsxCamera camera;
    double point[3]={123,-456,789},reference[3];
    long result;
    int i,k,j;
    camera.center[0]=camera.center[1]=camera.center[2]=0;
    for(i=0;i<4;i++) {
        camera.cy=angles[i][0]/4096.0;camera.sy=angles[i][1]/4096.0;
        camera.cp=angles[i][2]/4096.0;camera.sp=angles[i][3]/4096.0;
        psx_camera_rotation_fixed(angles[i][0],angles[i][1],angles[i][2],angles[i][3],matrix);
        psx_camera_view(&camera,point,reference);
        for(k=0;k<3;k++) {
            result=0;for(j=0;j<3;j++) result+=matrix[k][j]*(long)point[j];
            result=result/4096+(k==2?600:0);
            assert(fabs(result-reference[k])<2);
        }
    }
}

int main(void)
{
    PsxCamera c;
    double world[3],screen[3],back[3],view[3],near_point[3],far_point[3];
    double input[12][5]={{0,0,16,0,0},{60,0,64,60,0},{0,60,64,0,60}};
    double output[12][5];
    double a[3]={0,0,16},b[3]={60,30,64};
    long fixed_view[3]={40,20,300},fixed_screen[3];
    int mode,count,i,axis,side;
    fixed_clipping();fixed_rotation();
    c.center[0]=11;c.center[1]=27;c.center[2]=-13;
    c.cy=.8;c.sy=.6;c.cp=.8;c.sp=.6;c.zoom=.9;
    world[0]=100;world[1]=20;world[2]=80;
    for(mode=0;mode<2;mode++) {
        c.projection=mode;
        assert(psx_camera_project(&c,world,screen));
        assert(psx_camera_plane(&c,screen[0],screen[1],world[1],back));
        for(i=0;i<3;i++) close_to(world[i],back[i]);
        close_to(back[1],world[1]);
    }
    /* All six axis views: each plane round-trips in both projections. */
    for(axis=0;axis<3;axis++) for(side=-1;side<=1;side+=2) for(mode=0;mode<2;mode++) {
        c.projection=mode;c.cy=axis==0?0:side;c.sy=axis==0?side:0;
        c.cp=axis==1?0:1;c.sp=axis==1?side:0;
        assert(psx_camera_project(&c,world,screen));
        assert(psx_camera_plane_axis(&c,screen[0],screen[1],axis,world[axis],back));
        for(i=0;i<3;i++) close_to(world[i],back[i]);
    }
    c.cy=.8;c.sy=.6;c.cp=.8;c.sp=.6;
    view[0]=40;view[1]=20;view[2]=300;
    assert(psx_camera_project_view(1,1,view,near_point));
    view[2]=900;assert(psx_camera_project_view(1,1,view,far_point));
    close_to(near_point[0],3*far_point[0]);close_to(near_point[1],3*far_point[1]);
    assert(psx_camera_project_view(0,1,view,far_point));
    view[2]=300;assert(psx_camera_project_view(0,1,view,near_point));
    close_to(near_point[0],far_point[0]);close_to(near_point[1],far_point[1]);
    assert(psx_camera_project_fixed(1,4096,fixed_view,fixed_screen));
    assert(fixed_screen[0]==80 && fixed_screen[1]==40 && fixed_screen[2]==-300);
    fixed_view[2]=900;assert(psx_camera_project_fixed(0,4096,fixed_view,fixed_screen));
    assert(fixed_screen[0]==40 && fixed_screen[1]==20);
    fixed_view[2]=0;assert(!psx_camera_project_fixed(1,4096,fixed_view,fixed_screen));
    view[2]=0;assert(!psx_camera_project_view(1,1,view,screen));
    count=psx_camera_clip_near(input,3,output);assert(count==4);
    close_to(output[0][4],20);close_to(output[1][3],20);
    for(i=0;i<count;i++) {
        assert(output[i][2]>=PSX_CAMERA_NEAR);
        assert(output[i][3]>=0 && output[i][3]<=60);
        assert(output[i][4]>=0 && output[i][4]<=60);
    }
    for(i=0;i<3;i++) input[i][2]=16;
    assert(psx_camera_clip_near(input,3,output)==0);
    assert(psx_camera_clip_line(a,b));close_to(a[2],32);close_to(a[0],20);close_to(a[1],10);
    a[2]=b[2]=0;assert(!psx_camera_clip_line(a,b));
    c.projection=1;
    assert(!psx_camera_plane(&c,0,-450,0,back)); /* Ray parallel to the X/Z plane. */
    puts("C89 camera: projections, drag-plane intersection and integer clipping passed.");
    return 0;
}
