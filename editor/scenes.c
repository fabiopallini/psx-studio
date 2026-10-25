#include "studio.h"
#include <string.h>

static gboolean invalid(GError **error,const char *message)
{ g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,message);return FALSE; }
static JsonNode *clone_node(JsonNode *source)
{
    /* json_node_copy only refs object/array containers; history needs independent trees. */
    char *text=json_to_string(source,FALSE);JsonParser *parser=json_parser_new();
    gboolean ok=json_parser_load_from_data(parser,text,-1,NULL);
    JsonNode *copy=ok?json_node_copy(json_parser_get_root(parser)):NULL;
    g_object_unref(parser);g_free(text);return copy;
}
Scene *project_scene_find(const Project *p,guint id)
{
    for(guint i=0;i<p->scenes->len;i++) {Scene *s=g_ptr_array_index(p->scenes,i);if(s->id==id) return s;}
    return NULL;
}
static void transfer_view(Project *p,Project *q)
{
    g_ptr_array_unref(p->objects);p->objects=q->objects;q->objects=NULL;
    p->next_id=q->next_id;p->active_camera=q->active_camera;
    memcpy(p->camera,q->camera,sizeof(p->camera));memcpy(p->view,q->view,sizeof(p->view));
    p->projection=q->projection;p->drag_axis=q->drag_axis;
    p->show_game_frame=q->show_game_frame;p->show_colliders=q->show_colliders;
}
JsonNode *project_to_json(const Project *p)
{
    JsonNode *root=project_scene_to_json(p);JsonObject *doc=json_node_get_object(root);
    JsonArray *array=json_array_new();
    for(guint i=0;i<p->scenes->len;i++) {
        Scene *s=g_ptr_array_index(p->scenes,i);JsonObject *entry=json_object_new();
        json_object_set_int_member(entry,"id",s->id);json_object_set_string_member(entry,"name",s->name);
        json_object_set_member(entry,"data",s->id==p->scene_id?project_scene_to_json(p):clone_node(s->data));
        json_array_add_object_element(array,entry);
    }
    json_object_set_array_member(doc,"scenes",array);
    json_object_set_int_member(doc,"active_scene",p->scene_id);json_object_set_int_member(doc,"startup_scene",p->startup_scene);
    return root;
}
static gboolean integer(JsonObject *o,const char *key,guint *value)
{
    JsonNode *n=json_object_get_member(o,key);
    if(!n || !JSON_NODE_HOLDS_VALUE(n) || json_node_get_value_type(n)!=G_TYPE_INT64) return FALSE;
    gint64 x=json_node_get_int(n);if(x<1 || x>=G_MAXINT) return FALSE;*value=x;return TRUE;
}
static gboolean decode_view(Project *view,JsonNode *source,GError **error)
{
    JsonNode *node=clone_node(source);JsonObject *doc=json_node_get_object(node);
    /* Snapshots hold scene content; project-wide settings always come from the root. */
    json_object_set_string_member(doc,"name","Scene data");json_object_set_string_member(doc,"psyq_path","");
    json_object_set_string_member(doc,"emulator_command","");
    gboolean ok=project_scene_decode(view,node,error);json_node_free(node);return ok;
}
gboolean project_scene_view(const Project *p,guint index,Project *view,GError **error)
{
    project_init(view);
    if(index>=p->scenes->len) return invalid(error,"Invalid Scene index.");
    Scene *s=g_ptr_array_index(p->scenes,index);
    JsonNode *node=s->id==p->scene_id?project_scene_to_json(p):clone_node(s->data);
    gboolean ok=decode_view(view,node,error);json_node_free(node);
    if(ok) {
        g_free(view->path);view->path=g_strdup(p->path);g_free(view->asset_root);view->asset_root=g_strdup(p->asset_root);
        g_free(view->name);view->name=g_strdup(s->name);view->scene_id=s->id;
    }
    return ok;
}
gboolean project_from_json(Project *p,JsonNode *root,GError **error)
{
    if(!project_scene_decode(p,root,error)) return FALSE;
    JsonObject *doc=json_node_get_object(root);
    if(!json_object_has_member(doc,"scenes")) {Scene *first=g_ptr_array_index(p->scenes,0);g_free(first->name);first->name=g_strdup(p->name);return TRUE;}
    JsonNode *node=json_object_get_member(doc,"scenes");
    if(!JSON_NODE_HOLDS_ARRAY(node)) return invalid(error,"Scenes must be an array.");
    JsonArray *array=json_node_get_array(node);guint count=json_array_get_length(array),active,start;
    if(!count || count>STUDIO_MAX_SCENES || !integer(doc,"active_scene",&active) || !integer(doc,"startup_scene",&start))
        return invalid(error,"Invalid Scene count, selection or startup Scene.");
    g_ptr_array_set_size(p->scenes,0);p->next_scene_id=1;
    for(guint i=0;i<count;i++) {
        JsonNode *n=json_array_get_element(array,i);if(!JSON_NODE_HOLDS_OBJECT(n)) return invalid(error,"Invalid Scene.");
        JsonObject *entry=json_node_get_object(n);guint id;
        JsonNode *name=json_object_get_member(entry,"name"),*data=json_object_get_member(entry,"data");
        if(!integer(entry,"id",&id) || project_scene_find(p,id) || !name || !JSON_NODE_HOLDS_VALUE(name) ||
           json_node_get_value_type(name)!=G_TYPE_STRING || !*json_node_get_string(name) || !data || !JSON_NODE_HOLDS_OBJECT(data) ||
           json_object_has_member(json_node_get_object(data),"scenes")) return invalid(error,"Invalid or duplicate Scene.");
        Scene *s=g_new0(Scene,1);s->id=id;s->name=g_strdup(json_node_get_string(name));s->data=clone_node(data);
        g_ptr_array_add(p->scenes,s);p->next_scene_id=MAX(p->next_scene_id,id+1);
    }
    if(!project_scene_find(p,active) || !project_scene_find(p,start)) return invalid(error,"Scene reference does not exist.");
    p->scene_id=active;p->startup_scene=start;
    Scene *s=project_scene_find(p,active);Project view;project_init(&view);
    gboolean ok=decode_view(&view,s->data,error);
    if(ok) transfer_view(p,&view);
    project_clear(&view);
    return ok && project_validate(p,error);
}
gboolean project_scene_switch(Project *p,guint id,GError **error)
{
    Scene *target=project_scene_find(p,id),*current=project_scene_find(p,p->scene_id);
    if(!target) return invalid(error,"Scene does not exist.");
    if(id==p->scene_id) return TRUE;
    Project view;project_init(&view);gboolean ok=decode_view(&view,target->data,error);
    if(ok) {
        if(current->data) json_node_free(current->data);
        current->data=project_scene_to_json(p);
        transfer_view(p,&view);p->scene_id=id;
    }
    project_clear(&view);return ok;
}
Scene *project_scene_add(Project *p,const char *name)
{
    if(p->scenes->len>=STUDIO_MAX_SCENES || p->next_scene_id>=G_MAXINT) return NULL;
    Project blank;project_init(&blank);Scene *s=g_new0(Scene,1);
    s->id=p->next_scene_id++;s->name=name && *name?g_strdup(name):g_strdup_printf("Scene %u",s->id);
    s->data=project_scene_to_json(&blank);project_clear(&blank);g_ptr_array_add(p->scenes,s);
    project_scene_switch(p,s->id,NULL);p->dirty=TRUE;return s;
}
gboolean project_scene_delete(Project *p,guint id,GError **error)
{
    if(p->scenes->len<2) return invalid(error,"A project must contain at least one Scene.");
    Scene *s=project_scene_find(p,id);if(!s) return invalid(error,"Scene does not exist.");
    if(p->scene_id==id) {
        Scene *other=g_ptr_array_index(p->scenes,0);if(other==s) other=g_ptr_array_index(p->scenes,1);
        if(!project_scene_switch(p,other->id,error)) return FALSE;
    }
    g_ptr_array_remove(p->scenes,s);
    if(p->startup_scene==id) p->startup_scene=((Scene*)g_ptr_array_index(p->scenes,0))->id;
    /* Remove stale transition destinations in every remaining Scene. */
    for(guint i=0;i<p->scenes->len;i++) {
        Scene *remaining=g_ptr_array_index(p->scenes,i);
        if(remaining->id==p->scene_id) {
            for(guint k=0;k<p->objects->len;k++) {Object *o=g_ptr_array_index(p->objects,k);if(o->target_scene==id) {o->target_scene=0;if(o->collision_action==3) o->collision_action=0;}}
        } else {
            JsonArray *objects=json_object_get_array_member(json_node_get_object(remaining->data),"objects");
            for(guint k=0;k<json_array_get_length(objects);k++) {JsonObject *o=json_array_get_object_element(objects,k);
                if(json_object_has_member(o,"target_scene") && json_object_get_int_member(o,"target_scene")==id) {
                    json_object_set_int_member(o,"target_scene",0);
                    if(json_object_has_member(o,"collision_action") && json_object_get_int_member(o,"collision_action")==3) json_object_set_int_member(o,"collision_action",0);
                }
            }
        }
    }
    p->dirty=TRUE;return TRUE;
}

gboolean project_validate_scene(const Project *p,GError **error);
gboolean project_validate(const Project *p,GError **error)
{
    if(!project_validate_scene(p,error)) return FALSE;
    if(!p->scenes || !p->scenes->len || p->scenes->len>STUDIO_MAX_SCENES ||
       !project_scene_find(p,p->scene_id) || !project_scene_find(p,p->startup_scene)) return invalid(error,"Invalid Scene configuration.");
    for(guint i=0;i<p->scenes->len;i++) {
        Scene *scene=g_ptr_array_index(p->scenes,i);
        if(!scene->id || scene->id>=G_MAXINT || !scene->name || !*scene->name) return invalid(error,"Invalid Scene name or ID.");
        for(guint j=0;j<i;j++) if(((Scene*)g_ptr_array_index(p->scenes,j))->id==scene->id) return invalid(error,"Duplicate Scene IDs.");
        Project view;gboolean ok=project_scene_view(p,i,&view,error);
        if(ok) for(guint k=0;k<view.objects->len;k++) {Object *o=g_ptr_array_index(view.objects,k);
            if((o->target_scene && !project_scene_find(p,o->target_scene)) || (o->collision_action==3 && !o->target_scene)) {ok=invalid(error,"Choose an existing destination for Change Scene.");break;}}
        project_clear(&view);if(!ok) return FALSE;
    }
    return TRUE;
}
