#include "camera.h"

#ifndef PSX_RUNTIME_FIXED_ONLY
void psx_camera_view(const PsxCamera *c, const double world[3], double view[3])
{
    double x, y, z, depth;
    x=world[0]-c->center[0];y=world[1]-c->center[1];z=world[2]-c->center[2];
    depth=c->sy*x+c->cy*z;
    view[0]=c->cy*x-c->sy*z;
    view[1]=c->sp*depth-c->cp*y;
    view[2]=PSX_CAMERA_DISTANCE-c->cp*depth-c->sp*y;
}

int psx_camera_project_view(int projection, double zoom, const double view[3], double screen[3])
{
    double factor;
    factor=zoom;
    if(projection==PSX_PROJECTION_PERSPECTIVE) {
        if(view[2]<PSX_CAMERA_NEAR) return 0;
        factor*=PSX_CAMERA_DISTANCE/view[2];
    }
    screen[0]=view[0]*factor;screen[1]=view[1]*factor;
    screen[2]=view[2]-PSX_CAMERA_DISTANCE;
    return 1;
}

int psx_camera_project(const PsxCamera *c, const double world[3], double screen[3])
{
    double view[3];psx_camera_view(c,world,view);
    return psx_camera_project_view(c->projection,c->zoom,view,screen);
}

int psx_camera_plane_axis(const PsxCamera *c, double sx, double sy,
                          int axis, double coordinate, double world[3])
{
    double right[3],down[3],out[3],origin[3],ray[3],t,screen[3];
    int k;
    if(c->zoom<=0 || axis<0 || axis>2) return 0;
    right[0]=c->cy;right[1]=0;right[2]=-c->sy;
    down[0]=c->sy*c->sp;down[1]=-c->cp;down[2]=c->cy*c->sp;
    out[0]=c->sy*c->cp;out[1]=c->sp;out[2]=c->cy*c->cp;
    sx/=c->zoom;sy/=c->zoom;
    for(k=0;k<3;k++) {
        origin[k]=c->center[k]+PSX_CAMERA_DISTANCE*out[k];
        ray[k]=-out[k];
        if(c->projection==PSX_PROJECTION_PERSPECTIVE)
            ray[k]+=(right[k]*sx+down[k]*sy)/PSX_CAMERA_DISTANCE;
        else origin[k]+=right[k]*sx+down[k]*sy;
    }
    if(ray[axis]>-0.00000001 && ray[axis]<0.00000001) return 0;
    t=(coordinate-origin[axis])/ray[axis];
    for(k=0;k<3;k++) world[k]=origin[k]+t*ray[k];
    return psx_camera_project(c,world,screen);
}

int psx_camera_plane(const PsxCamera *c, double sx, double sy, double world_y, double world[3])
{
    return psx_camera_plane_axis(c,sx,sy,1,world_y,world);
}

int psx_camera_clip_near(double input[12][5], int count, double output[12][5])
{
    int i,k,previous,inside,was_inside,result;
    double ratio;
    result=0;
    for(i=0;i<count;i++) {
        previous=i?i-1:count-1;
        inside=input[i][2]>=PSX_CAMERA_NEAR;was_inside=input[previous][2]>=PSX_CAMERA_NEAR;
        if(inside!=was_inside) {
            ratio=(PSX_CAMERA_NEAR-input[previous][2])/(input[i][2]-input[previous][2]);
            for(k=0;k<5;k++) output[result][k]=input[previous][k]+ratio*(input[i][k]-input[previous][k]);
            output[result][2]=PSX_CAMERA_NEAR;result++;
        }
        if(inside) {for(k=0;k<5;k++) output[result][k]=input[i][k];result++;}
    }
    return result;
}

int psx_camera_clip_line(double a[3], double b[3])
{
    double ratio;
    double *outside;
    double *inside;
    int k;
    if(a[2]<PSX_CAMERA_NEAR && b[2]<PSX_CAMERA_NEAR) return 0;
    if(a[2]>=PSX_CAMERA_NEAR && b[2]>=PSX_CAMERA_NEAR) return 1;
    outside=a[2]<PSX_CAMERA_NEAR?a:b;inside=outside==a?b:a;
    ratio=(PSX_CAMERA_NEAR-outside[2])/(inside[2]-outside[2]);
    for(k=0;k<3;k++) outside[k]+=ratio*(inside[k]-outside[k]);
    outside[2]=PSX_CAMERA_NEAR;
    return 1;
}

#endif /* Desktop camera helpers. */

int psx_camera_project_fixed(int projection, long zoom, const long view[3], long screen[3])
{
    int axis;
    long scaled;
    if(projection==PSX_PROJECTION_PERSPECTIVE && view[2]<(long)PSX_CAMERA_NEAR) return 0;
    for(axis=0;axis<2;axis++) {
        scaled=(view[axis]/4096)*zoom+(view[axis]%4096)*zoom/4096;
        screen[axis]=projection==PSX_PROJECTION_PERSPECTIVE?scaled*600/view[2]:scaled;
    }
    screen[2]=view[2]-600;
    return 1;
}

void psx_camera_rotation_fixed(long cy,long sy,long cp,long sp,short matrix[3][3])
{
    matrix[0][0]=(short)cy;matrix[0][1]=0;matrix[0][2]=(short)-sy;
    matrix[1][0]=(short)(sp*sy/4096);matrix[1][1]=(short)-cp;matrix[1][2]=(short)(sp*cy/4096);
    matrix[2][0]=(short)(-cp*sy/4096);matrix[2][1]=(short)-sp;matrix[2][2]=(short)(-cp*cy/4096);
}

/* numerator/denominator is in [0,1]. Avoid software floating point and
 * overflowing delta*numerator on the console's 32-bit long type. */
static long interpolate_fixed(long a,long b,long numerator,long denominator)
{
    long delta;
    unsigned long magnitude,n,d,result,remainder,bit;
    int negative;
    if(denominator<0) {numerator=-numerator;denominator=-denominator;}
    if(!numerator) return a;
    if(numerator==denominator) return b;
    delta=b-a;negative=delta<0;
    magnitude=(unsigned long)(negative?-delta:delta);
    n=(unsigned long)numerator;d=(unsigned long)denominator;
    if(!magnitude) return a;
    if(magnitude<=2147483647UL/n) result=magnitude*n/d;
    else {
        /* Binary long division of magnitude*n without materializing the
         * product. n<=d ensures the final quotient fits magnitude. */
        result=0;remainder=0;
        for(bit=0x80000000UL;bit;bit>>=1) {
            result*=2;
            if(remainder>=d-remainder) {remainder-=d-remainder;result++;}
            else remainder*=2;
            if(magnitude&bit) {
                if(remainder>=d-n) {remainder-=d-n;result++;}
                else remainder+=n;
            }
        }
    }
    return a+(negative?-(long)result:(long)result);
}

int psx_camera_clip_polygon_fixed(long input[12][5],int count,
    long output[12][5],int axis,long edge,int greater)
{
    int i,k,previous,inside,was_inside,result;
    long numerator,denominator;
    result=0;
    for(i=0;i<count;i++) {
        previous=i?i-1:count-1;
        inside=greater?input[i][axis]>=edge:input[i][axis]<=edge;
        was_inside=greater?input[previous][axis]>=edge:input[previous][axis]<=edge;
        if(inside!=was_inside) {
            numerator=edge-input[previous][axis];denominator=input[i][axis]-input[previous][axis];
            for(k=0;k<5;k++) output[result][k]=interpolate_fixed(input[previous][k],input[i][k],numerator,denominator);
            output[result][axis]=edge;result++;
        }
        if(inside) {for(k=0;k<5;k++) output[result][k]=input[i][k];result++;}
    }
    return result;
}

int psx_camera_clip_line_fixed(long a[5],long b[5],int axis,long edge,int greater)
{
    int first,second,k;
    long numerator,denominator;
    long *outside,*inside;
    first=greater?a[axis]>=edge:a[axis]<=edge;
    second=greater?b[axis]>=edge:b[axis]<=edge;
    if(!first && !second) return 0;
    if(first && second) return 1;
    outside=first?b:a;inside=first?a:b;
    numerator=edge-outside[axis];denominator=inside[axis]-outside[axis];
    for(k=0;k<5;k++) outside[k]=interpolate_fixed(outside[k],inside[k],numerator,denominator);
    outside[axis]=edge;
    return 1;
}
