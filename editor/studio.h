#ifndef STUDIO_H
#define STUDIO_H

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <epoxy/gl.h>
#include "../runtime/camera.h"

#define STUDIO_APP_VERSION "0.5.1"
#define STUDIO_VERSION 3
#define STUDIO_MAX_SCENES 16
#define STUDIO_MAX_OBJECTS 512

typedef enum { OBJECT_MESH, OBJECT_SPRITE, OBJECT_BILLBOARD, OBJECT_CAMERA } ObjectKind;
typedef struct { double p[3], uv[2]; } AssetVertex;
typedef struct {
    GArray *vertices;
    unsigned char *pixels;
    guint width, height;
    GLuint texture_id;
    gboolean has_uv;
} Asset;
typedef struct {
    guint id;
    char *name;
    ObjectKind kind;
    double position[3], rotation[3], scale[3], bounds[3], collider_offset[3], follow_offset[3];
    guint follow_target, target_scene;
    gboolean solid;
    char *asset, *module, *texture;
    int input_mode, collision_action, speed;
    double camera_zoom;
    int camera_projection;
    Asset *visual;
    gboolean visual_attempted;
} Object;
typedef struct { guint id; char *name; JsonNode *data; } Scene;
typedef struct {
    char *name, *path, *psyq_path, *asset_root;
    char *dosbox, *output_path, *emulator_command;
    GPtrArray *objects;
    guint next_id;
    double camera[3];
    double view[3]; /* Yaw degrees, pitch degrees, zoom. */
    int projection, drag_axis;
    guint active_camera;
    gboolean show_game_frame;
    gboolean show_colliders;
    GPtrArray *scenes;
    guint scene_id, startup_scene, next_scene_id;
    gboolean dirty;
} Project;
typedef struct {
    GtkApplication *application;
    GtkWidget *window, *title_label, *list, *viewport, *status, *name, *kind, *solid;
    GtkWidget *numbers[4][3], *psyq, *log, *project_name;
    Project project;
    int selected;
    gboolean refreshing;
    GLuint program, vao, vbo;
    double zoom, yaw, pitch, drag_x, drag_z, orbit_yaw, orbit_pitch;
    double drag_screen_x, drag_screen_y, drag_anchor[3], drag_start[3];
    gboolean dragging;
    char *smoke_path;
    char *root, *executable;
    GtkWidget *menubar, *body, *projection_button, *frame_button, *view_preset, *drag_plane, *drag_hint;
    GtkWidget *scale_panel, *camera_panel, *camera_active, *camera_zoom, *camera_projection;
    GtkWidget *visual_imports[2];
    GtkWidget *scene_selector, *scene_name, *startup_button, *collision_scene, *follow_target;
    GtkWidget *collider_offset[3], *follow_offset[3], *colliders_button;
    GPtrArray *undo, *redo;gpointer history_key;gint64 history_time;gboolean restoring;
    GtkWidget *log_panel;
    char *output_directory;
    GSubprocess *build_process, *emulator_process;
    GtkWidget *emulator_entry;
    gboolean build_and_exit;
    GtkWidget *texture_label, *asset_label, *module_label, *input_mode, *collision_action, *speed;
    GtkWidget *camera_values[6], *dosbox, *settings_window, *settings_status, *sdk_source;
} Studio;

void project_init(Project *p);
void project_clear(Project *p);
Object *project_add(Project *p, ObjectKind kind);
Object *project_duplicate(Project *p, guint index);
void project_camera_from_view(Project *p, Object *o);
void project_camera_position(const Project *p,const Object *o,double position[3]);
JsonNode *project_scene_to_json(const Project *p);
JsonNode *project_to_json(const Project *p);
gboolean project_scene_decode(Project *p,JsonNode *node,GError **error);
gboolean project_from_json(Project *p,JsonNode *node,GError **error);
Scene *project_scene_find(const Project *p,guint id);
Scene *project_scene_add(Project *p,const char *name);
gboolean project_scene_switch(Project *p,guint id,GError **error);
gboolean project_scene_delete(Project *p,guint id,GError **error);
gboolean project_scene_view(const Project *p,guint index,Project *view,GError **error);
void studio_checkpoint(Studio *s,gpointer key);
gboolean project_load(Project *p, const char *path, GError **error);
gboolean project_save(Project *p, const char *path, GError **error);
char *project_data_path(const char *path);
char *project_build_output(const Project *project, const char *root);
char *project_build_sdk(const Project *project, const char *root);
char **project_emulator_argv(const char *command,const char *cue,const char *iso,GError **error);
gboolean project_validate(const Project *p, GError **error);
gboolean project_export(const Project *p, const char *directory, GError **error);
int studio_build_main(int argc, char **argv);
char *studio_install_root(const char *executable);
void studio_refresh(Studio *s);
void studio_status(Studio *s, const char *message);
GtkWidget *viewport_create(Studio *s);
Asset *asset_load(const Project *p, const Object *o, GError **error);
void asset_free(Asset *asset);
long asset_uv_texel(double coordinate,guint size);
gboolean asset_write_tim(const Asset *asset, const char *path, int x, int y, GError **error);
gboolean asset_import(Project *p, Object *o, const char *source, int type, GError **error);
char *asset_path(const Project *p, const char *relative);
void viewport_clear_assets(Studio *s);
gboolean viewport_project(const Studio *s, const double point[3], double *x, double *y);

#endif
