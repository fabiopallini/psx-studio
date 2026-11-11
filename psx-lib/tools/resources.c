#include "core.h"
#include <string.h>

static gboolean fail(GError **error,const char *message)
{ g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,message);return FALSE; }
static void model_free(gpointer data)
{ ResourceModel *model=data;g_free(model->asset);g_free(model->texture);g_free(model); }
static void layer_free(gpointer data)
{ ResourceLayer *layer=data;g_free(layer->path);g_free(layer); }
void resource_context_init(ResourceContext *context)
{
    memset(context,0,sizeof(*context));context->background=g_strdup("");
    context->models=g_ptr_array_new_with_free_func(model_free);
    context->background_layers=g_ptr_array_new_with_free_func(layer_free);
}
void resource_context_clear(ResourceContext *context)
{
    g_free(context->asset_root);g_free(context->background);
    g_ptr_array_unref(context->models);g_ptr_array_unref(context->background_layers);
    memset(context,0,sizeof(*context));
}
ResourceModel *resource_model_add(ResourceContext *context,ResourceKind kind)
{
    if(context->models->len>=RESOURCE_MAX_MODELS) return NULL;
    ResourceModel *model=g_new0(ResourceModel,1);model->kind=kind;
    model->id=context->models->len+1;model->asset=g_strdup("");model->texture=g_strdup("");
    g_ptr_array_add(context->models,model);return model;
}
static gboolean relative_asset(const char *path)
{
    if(!path || g_path_is_absolute(path) || strchr(path,'\\') || strchr(path,':')) return FALSE;
    char **parts=g_strsplit(path,"/",-1);gboolean valid=TRUE;
    for(guint i=0;parts[i];i++) if(!strcmp(parts[i],"..")) valid=FALSE;
    g_strfreev(parts);return valid;
}
gboolean resource_context_validate(const ResourceContext *context,GError **error)
{
    if(!context->asset_root || !relative_asset(context->background)) return fail(error,"Resources require an asset root and relative paths without parent traversal.");
    if(context->models->len>RESOURCE_MAX_MODELS || context->background_layers->len>RESOURCE_MAX_LAYERS)
        return fail(error,"Too many resource models or depth layers.");
    for(guint i=0;i<context->models->len;i++) {
        ResourceModel *model=g_ptr_array_index(context->models,i);
        if(!model->id || model->id>=G_MAXINT || model->kind<RESOURCE_MESH || model->kind>RESOURCE_BILLBOARD ||
           !relative_asset(model->asset) || !relative_asset(model->texture) || (!*model->asset && !*model->texture))
            return fail(error,"Invalid resource model ID, kind or path.");
        for(guint j=0;j<i;j++) if(((ResourceModel*)g_ptr_array_index(context->models,j))->id==model->id)
            return fail(error,"Duplicate resource model IDs.");
    }
    for(guint i=0;i<context->background_layers->len;i++) {
        ResourceLayer *layer=g_ptr_array_index(context->background_layers,i);
        if(!*layer->path || !relative_asset(layer->path) || layer->depth<16 || layer->depth>32767)
            return fail(error,"Depth layers require a relative PNG and depth from 16 to 32767.");
    }
    return TRUE;
}
