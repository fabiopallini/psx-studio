#ifndef STUDIO_CAMERA_H
#define STUDIO_CAMERA_H

#define PSX_PROJECTION_ORTHOGRAPHIC 0
#define PSX_PROJECTION_PERSPECTIVE 1
#define PSX_CAMERA_DISTANCE 600L
#define PSX_CAMERA_NEAR 32L
#define PSX_CAMERA_FAR 262144L

#ifndef PSX_RUNTIME_FIXED_ONLY
typedef struct PsxCamera {
    double center[3];
    double cy, sy, cp, sp;
    double zoom;
    int projection;
} PsxCamera;
#endif

/* Fixed-point console path: zoom=4096 means 1.0; validated scene range. */
int psx_camera_bounds_outside_fixed(int projection,long zoom,const long view[3],long radius);
int psx_camera_project_fixed(int projection, long zoom, const long view[3], long screen[3]);

/* Q12 camera rotation for GTE/local view transforms; output is Q12. */
void psx_camera_rotation_fixed(long cy,long sy,long cp,long sp,short matrix[3][3]);

/* Console clipping with integer positions/depth/UVs. Inputs use at most 12
 * vertices; interpolation avoids overflowing a signed 32-bit product. */
int psx_camera_clip_polygon_fixed(long input[12][5], int count,
    long output[12][5], int axis, long edge, int greater);
int psx_camera_clip_line_fixed(long a[5], long b[5],
    int axis, long edge, int greater);

#ifndef PSX_RUNTIME_FIXED_ONLY
void psx_camera_view(const PsxCamera *camera, const double world[3], double view[3]);
int psx_camera_project_view(int projection, double zoom, const double view[3], double screen[3]);
int psx_camera_project(const PsxCamera *camera, const double world[3], double screen[3]);
int psx_camera_plane(const PsxCamera *camera, double screen_x, double screen_y,
                     double world_y, double world[3]);
int psx_camera_plane_axis(const PsxCamera *camera, double screen_x, double screen_y,
                          int fixed_axis, double coordinate, double world[3]);
int psx_camera_clip_near(double input[12][5], int count, double output[12][5]);
int psx_camera_clip_line(double a[3], double b[3]);

#endif /* Desktop camera helpers. */

#endif
