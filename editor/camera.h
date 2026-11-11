#ifndef STUDIO_CAMERA_H
#define STUDIO_CAMERA_H

#define PSX_PROJECTION_ORTHOGRAPHIC 0
#define PSX_PROJECTION_PERSPECTIVE 1
#define PSX_CAMERA_DISTANCE 600L
#define PSX_CAMERA_NEAR 32L
#define PSX_CAMERA_FAR 262144L

typedef struct PsxCamera {
    double center[3];
    double cy, sy, cp, sp;
    double zoom;
    int projection;
} PsxCamera;

void psx_camera_view(const PsxCamera *camera, const double world[3], double view[3]);
int psx_camera_project_view(int projection, double zoom, const double view[3], double screen[3]);
int psx_camera_project(const PsxCamera *camera, const double world[3], double screen[3]);
int psx_camera_plane(const PsxCamera *camera, double screen_x, double screen_y,
                     double world_y, double world[3]);
int psx_camera_plane_axis(const PsxCamera *camera, double screen_x, double screen_y,
                          int fixed_axis, double coordinate, double world[3]);
int psx_camera_clip_near(double input[12][5], int count, double output[12][5]);
int psx_camera_clip_line(double a[3], double b[3]);


#endif
