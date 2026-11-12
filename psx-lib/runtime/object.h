#ifndef STUDIO_OBJECT_H
#define STUDIO_OBJECT_H

/* Portable runtime core: strict ANSI C89, no editor or SDK dependencies. */
#define PSX_OBJECT_MESH 0
#define PSX_OBJECT_SPRITE 1
#define PSX_OBJECT_BILLBOARD 2
#define PSX_OBJECT_CAMERA 3
#define PSX_RENDER_BACKGROUND 1
#define PSX_RENDER_CULL_BACKFACES 2
#define PSX_RENDER_DECAL 4 /* Translucent green ground overlay, above backdrop. */
#define PSX_RENDER_ALERT 8 /* Orange instead of green decal. */
#define PSX_RENDER_NO_FALLBACK 128 /* No default box when mesh is absent; collision/logic remain active. */
#define PSX_RENDER_HIDDEN 64 /* Collision-only objects; alive/solid logic remains active. */
#define PSX_RENDER_EMISSIVE 32 /* Bright unlit textured effects (white-hot tips). */

typedef struct PsxObject PsxObject;
typedef struct PsxScene PsxScene;
typedef struct PsxVertex {
    long p[3];
    unsigned char uv[2];
} PsxVertex;
typedef struct PsxMesh {
    const PsxVertex *vertices; /* Flattened triangles. */
    unsigned int count;
    int texture;
} PsxMesh;
typedef struct PsxTexture {
    const char *path;
    int x, y, w, h;
    unsigned short tpage;
    unsigned char u, v;
} PsxTexture;

/* Immutable ADPCM data is resident in the EXE; address is assigned at Scene load. */
typedef struct PsxSound {
    const char *name;
    const unsigned char *data;
    unsigned long size,rate,address;
} PsxSound;

typedef struct PsxLogic {
    void (*init)(PsxObject *object, PsxScene *scene);
    void (*update)(PsxObject *object, PsxScene *scene, unsigned long input);
    void (*collision)(PsxObject *object, PsxObject *other, PsxScene *scene);
} PsxLogic;

struct PsxObject {
    unsigned int id;
    int kind;
    int alive;
    int solid;
    long position[3];
    long rotation[3]; /* 4096 units per turn. */
    long scale[3]; /* Fixed point: 4096 = 1.0. */
    long half_size[3];
    long collider_offset[3];
    unsigned int follow_target, target_scene;
    long follow_offset[3];
    void (*cleanup)(PsxObject *object, PsxScene *scene);
    const PsxLogic *logic;
    void *user_data;
    const PsxMesh *mesh;
    int input_mode;
    int collision_action;
    int speed;
    long camera_zoom;
    int camera_projection;
    int render_flags; /* PSX_RENDER_* bit flags; default is two-sided depth sorting. */
};

/* Generic, bounded 2D overlay commands in painter order. */
#define PSX_OVERLAY_CAPACITY 384
#define PSX_OVERLAY_SPRITES 16
#define PSX_OVERLAY_TEXT_SIZE 512
#define PSX_OVERLAY_RECT 1
#define PSX_OVERLAY_SPRITE 2
typedef struct PsxOverlayCommand {
    short x,y,w,h;
    unsigned short texture;
    unsigned char u,v;
    unsigned short source_w,source_h;
    unsigned char r,g,b,type;
} PsxOverlayCommand;
typedef struct PsxOverlay {
    unsigned int count,sprites;
    PsxOverlayCommand commands[PSX_OVERLAY_CAPACITY];
    char text[PSX_OVERLAY_TEXT_SIZE];
} PsxOverlay;
#define PSX_CHECKPOINT_LOAD 1
#define PSX_CHECKPOINT_SAVE 2
typedef struct PsxCheckpoint {
    int operation,status; /* Request: LOAD/SAVE; completion: 1 success, -1 failure. */
    unsigned int scene_id; /* Application-defined nonzero checkpoint ID. */
    const char *file,*title; /* Application-owned card namespace and display title. */
    unsigned char *data; /* Optional application payload, copied at the I/O boundary. */
    unsigned int size,capacity; /* SAVE size; LOAD capacity and resulting size. */
} PsxCheckpoint;

/* Raw left-stick axes after a radial dead zone, -128..127 (128 is centre
 * in the controller packet). Buttons retain the SDK PadRead bit layout. */
typedef struct PsxInput {
    int x,z,analog;
} PsxInput;
unsigned long psx_input_decode(PsxInput *input,const unsigned char packet[8]);
/* Angles: 4096 units/turn, 0 north (-Z), 1024 west (-X). Output vectors Q12.
 * psx_direction_angle inputs are bounded world axes [-32767,32767]. */
int psx_direction_angle(long x,long z);
void psx_direction_vector(int angle,long vector[2]);
int psx_direction_normalize(long x,long z,long vector[2]);

#define PSX_MAX_BACKDROPS 8
typedef struct PsxBackdrop {
    unsigned int texture; /* Local 0-based texture index. */
    int x,y,w,h; /* Native 320x256 screen rectangle. */
    long depth; /* Positive camera-space units, same units as projected geometry. */
} PsxBackdrop;

/* Generated asset banks contain resources only, with application-chosen mesh IDs. */
typedef struct PsxResourceMesh {
    unsigned int id;
    const PsxMesh *mesh;
} PsxResourceMesh;
typedef struct PsxResourceBank {
    PsxTexture *textures;
    unsigned int texture_count;
    const PsxResourceMesh *meshes;
    unsigned int mesh_count;
    PsxSound *sounds;
    unsigned int sound_count,background_texture,backdrop_count;
    PsxBackdrop backdrops[PSX_MAX_BACKDROPS];
} PsxResourceBank;
const PsxMesh *psx_resource_mesh(const PsxResourceBank *bank,unsigned int id);
void psx_resource_apply(PsxScene *scene,const PsxResourceBank *bank);

/* Optional Q12 object rotations. Inline Scene storage freezes with a frame.
 * The upper render_flags bits hold a 1-based slot; lower bits remain flags. */
#define PSX_OBJECT_ROTATION_CAPACITY 64
#define PSX_RENDER_ROTATION_SHIFT 8
#define PSX_RENDER_FLAGS_MASK 255
typedef struct PsxObjectRotation {short m[9];} PsxObjectRotation;
PsxObjectRotation *psx_object_rotation(PsxScene *scene,PsxObject *object);
const short *psx_object_rotation_find(const PsxScene *scene,const PsxObject *object);

struct PsxScene {
    PsxObject *objects;
    unsigned int count;
    long camera[3]; /* Look-at centre. */
    int yaw, pitch, projection;
    long zoom; /* 4096 = 1.0. */
    unsigned int active_camera; /* Object ID; 0 uses legacy scene view. */
    unsigned int scene_id, pending_scene;
    int show_colliders;
    int (*scene_exists)(unsigned int id);
    int camera_eye; /* Camera contains an eye position instead of a look-at centre. */
    const char *loading; /* Optional application-owned loading caption. */
    PsxCheckpoint checkpoint;
    PsxInput input;
    PsxSound *sounds;
    unsigned int sound_count;
    int (*play_music)(PsxScene *scene,const char *name,int volume);
    int (*play_sound)(PsxScene *scene,const char *name,int volume,int pan);
    unsigned int background_texture; /* 0 none; otherwise first of two screen-space TIM tiles, 1-based. */
    void (*update_game)(PsxScene *scene,unsigned long input);
    void (*prepare_overlay)(PsxScene *scene); /* Build commands before snapshot; no simulation. */
    PsxOverlay overlay;
    unsigned int backdrop_count;
    PsxBackdrop backdrops[PSX_MAX_BACKDROPS];
    unsigned int rotation_count;
    PsxObjectRotation rotations[PSX_OBJECT_ROTATION_CAPACITY];
};

/* Volume 0..16383, pan Q12 -4096..4096. No I/O/wait in this call. */
int psx_music_play(PsxScene *scene,const char *name,int volume);
int psx_sound_play(PsxScene *scene,const char *name,int volume,int pan);
void psx_overlay_clear(PsxScene *scene);
/* Return 0 when invalid/full. Textures are 0-based Scene texture indices. */
int psx_overlay_rect(PsxScene *scene,int x,int y,int w,int h,int r,int g,int b);
int psx_overlay_sprite(PsxScene *scene,unsigned int texture,int x,int y,int w,int h,
    int u,int v,int source_w,int source_h);
void psx_overlay_text(PsxScene *scene,const char *text);
void psx_scene_init(PsxScene *scene);
void psx_scene_clear(PsxScene *scene);
int psx_scene_request_change(PsxScene *scene,unsigned int id);
PsxObject *psx_scene_find(PsxScene *scene,unsigned int id);
void psx_scene_update(PsxScene *scene, unsigned long input);
int psx_objects_overlap(const PsxObject *a, const PsxObject *b);
int psx_object_move(PsxObject *object, PsxScene *scene, long dx, long dy, long dz);
void psx_object_kill(PsxObject *object);

#endif
