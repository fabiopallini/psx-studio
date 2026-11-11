#ifndef PSX_LIB_CORE_H
#define PSX_LIB_CORE_H
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#define RESOURCE_MAX_MODELS 512
#define RESOURCE_MAX_LAYERS 4

typedef enum { RESOURCE_MESH, RESOURCE_SPRITE, RESOURCE_BILLBOARD } ResourceKind;
typedef struct { double p[3], uv[2]; } AssetVertex;
typedef struct {
    GArray *vertices;
    unsigned char *pixels;
    guint width,height;
    gboolean has_uv;
} Asset;
typedef struct { guint id; ResourceKind kind; char *asset,*texture; } ResourceModel;
typedef struct { char *path; int depth; } ResourceLayer;
typedef struct {
    char *asset_root,*background;
    guint bank_id;
    GPtrArray *models,*background_layers;
} ResourceContext;
void resource_context_init(ResourceContext *context);
void resource_context_clear(ResourceContext *context);
ResourceModel *resource_model_add(ResourceContext *context,ResourceKind kind);
gboolean resource_context_validate(const ResourceContext *context,GError **error);
char *resource_read_source(const ResourceContext *context,const char *relative,GError **error);
gboolean resources_export(const char *list,const char *asset_root,const char *directory,GError **error);
int engine_build_main(int argc,char **argv);
char *engine_install_root(const char *executable);
Asset *asset_load(const ResourceContext *context,const ResourceModel *model,GError **error);
void asset_free(Asset *asset);
Asset *asset_load_background(const ResourceContext *context,GError **error);
Asset *asset_load_background_layer(const ResourceContext *context,const char *relative,GError **error);
long asset_uv_texel(double coordinate,guint size);
gboolean asset_write_tim(const Asset *asset,const char *path,int x,int y,GError **error);
char *asset_path(const ResourceContext *context,const char *relative);
#endif
