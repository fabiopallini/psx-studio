#include "studio.h"
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>

static gboolean fail(GError **error, const char *message)
{
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
    return FALSE;
}

static void object_free(gpointer data)
{
    Object *o = data;
    g_free(o->name); g_free(o->asset); g_free(o->module); g_free(o->texture); asset_free(o->visual); g_free(o);
}

static void scene_free(gpointer value)
{ Scene *s=value;g_free(s->name);if(s->data) json_node_free(s->data);g_free(s); }

void project_init(Project *p)
{
    memset(p, 0, sizeof(*p));
    p->name = g_strdup("New project");
    p->psyq_path = g_strdup("");
    p->emulator_command=g_strdup("pcsxr -cdfile {cue}");
    p->dosbox = g_strdup(""); p->output_path = g_strdup("");
    p->view[0] = .55 * 180 / G_PI; p->view[1] = .6 * 180 / G_PI; p->view[2] = .9;
    p->objects = g_ptr_array_new_with_free_func(object_free);
    p->next_id = 1;
    p->show_game_frame = TRUE; p->drag_axis = 1;
    p->camera[2] = -600;
    p->scenes=g_ptr_array_new_with_free_func(scene_free);
    Scene *scene=g_new0(Scene,1);scene->id=1;scene->name=g_strdup("Scene 1");
    g_ptr_array_add(p->scenes,scene);p->scene_id=p->startup_scene=1;p->next_scene_id=2;
}

void project_clear(Project *p)
{
    g_free(p->name); g_free(p->path); g_free(p->psyq_path); g_free(p->asset_root); g_free(p->dosbox); g_free(p->output_path); g_free(p->emulator_command);
    g_clear_pointer(&p->objects, g_ptr_array_unref);
    g_clear_pointer(&p->scenes,g_ptr_array_unref);
    memset(p, 0, sizeof(*p));
}

void project_camera_position(const Project *p,const Object *o,double position[3])
{
    memcpy(position,o->position,sizeof(o->position));
    if(o->kind==OBJECT_CAMERA && o->follow_target) for(guint i=0;i<p->objects->len;i++) {
        Object *target=g_ptr_array_index(p->objects,i);
        if(target->id==o->follow_target) {for(int k=0;k<3;k++) position[k]=CLAMP(target->position[k]+o->follow_offset[k],-32767,32767);return;}
    }
}

void project_camera_from_view(Project *p, Object *o)
{
    double yaw=p->view[0]*G_PI/180, pitch=p->view[1]*G_PI/180;
    double direction[]={sin(yaw)*cos(pitch),sin(pitch),cos(yaw)*cos(pitch)};
    for(int k=0;k<3;k++) {
        o->position[k]=CLAMP(p->camera[k]+(k==2?600:0)+600*direction[k],-32767,32767);
        o->rotation[k]=k==0?p->view[1]:k==1?p->view[0]:0;
    }
    if(o->follow_target) for(guint i=0;i<p->objects->len;i++) {Object *target=g_ptr_array_index(p->objects,i);
        if(target->id==o->follow_target) for(int k=0;k<3;k++) o->follow_offset[k]=o->position[k]-target->position[k];}
    o->camera_zoom=p->view[2];o->camera_projection=p->projection;
}

Object *project_add(Project *p, ObjectKind kind)
{
    if (p->objects->len >= STUDIO_MAX_OBJECTS || p->next_id >= G_MAXINT) return NULL;
    Object *o = g_new0(Object, 1);
    o->id = p->next_id++;
    o->kind = kind;
    o->name = g_strdup_printf("Object %u", o->id);
    o->asset = g_strdup(""); o->module = g_strdup(""); o->texture = g_strdup(""); o->speed = 2;
    for (int i = 0; i < 3; i++) { o->scale[i] = 1; o->bounds[i] = 32; }
    o->camera_zoom=1;
    if(kind==OBJECT_CAMERA) {
        g_free(o->name);o->name=g_strdup_printf("Camera %u",o->id);
        project_camera_from_view(p,o);
        for(int k=0;k<3;k++) o->bounds[k]=16;
        gboolean first=TRUE;
        for(guint i=0;i<p->objects->len;i++)
            if(((Object*)g_ptr_array_index(p->objects,i))->kind==OBJECT_CAMERA) first=FALSE;
        if(first && !p->active_camera) p->active_camera=o->id;
    }
    g_ptr_array_add(p->objects, o);
    p->dirty = TRUE;
    return o;
}

Object *project_duplicate(Project *p, guint index)
{
    if (index >= p->objects->len) return NULL;
    Object *source = g_ptr_array_index(p->objects, index);
    Object *copy = project_add(p, source->kind);
    if (!copy) return NULL;
    g_free(copy->name); copy->name = g_strdup_printf("%s copy", source->name);
    g_free(copy->asset); copy->asset = g_strdup(source->asset);
    g_free(copy->module); copy->module = g_strdup(source->module);
    memcpy(copy->position, source->position, sizeof(copy->position));
    memcpy(copy->rotation, source->rotation, sizeof(copy->rotation));
    memcpy(copy->scale, source->scale, sizeof(copy->scale));
    memcpy(copy->bounds, source->bounds, sizeof(copy->bounds));
    copy->position[0] += 40;
    copy->solid = source->solid;
    g_free(copy->texture); copy->texture = g_strdup(source->texture);
    copy->input_mode = source->input_mode; copy->collision_action = source->collision_action; copy->speed = source->speed;
    memcpy(copy->collider_offset,source->collider_offset,sizeof(copy->collider_offset));
    memcpy(copy->follow_offset,source->follow_offset,sizeof(copy->follow_offset));
    copy->follow_target=source->follow_target;copy->target_scene=source->target_scene;
    copy->camera_zoom=source->camera_zoom;copy->camera_projection=source->camera_projection;
    return copy;
}

char *project_data_path(const char *path)
{
    char *base = g_strdup(path);
    if (g_str_has_suffix(base, ".psx")) base[strlen(base) - 4] = '\0';
    char *result = g_strconcat(base, "_psx_data", NULL);
    g_free(base);
    return result;
}

static gboolean relative_asset(const char *path)
{
    if (!path || g_path_is_absolute(path) || strchr(path, '\\') || strchr(path, ':')) return FALSE;
    char **parts = g_strsplit(path, "/", -1);
    gboolean valid = TRUE;
    for (int i = 0; parts[i]; i++) if (g_str_equal(parts[i], "..")) valid = FALSE;
    g_strfreev(parts);
    return valid;
}

char **project_emulator_argv(const char *command,const char *cue,const char *iso,GError **error)
{
    char **argv=NULL;int argc;
    if(!command || !*command) {fail(error,"Configure an emulator command in Settings.");return NULL;}
    if(!g_shell_parse_argv(command,&argc,&argv,error)) return NULL;
    gboolean image=FALSE;
    for(int i=0;i<argc;i++) {
        if(strstr(argv[i],"{cue}") || strstr(argv[i],"{iso}")) image=TRUE;
        GString *value=g_string_new("");
        for(const char *at=argv[i];*at;) {
            if(g_str_has_prefix(at,"{cue}")) {g_string_append(value,cue);at+=5;}
            else if(g_str_has_prefix(at,"{iso}")) {g_string_append(value,iso);at+=5;}
            else g_string_append_c(value,*at++);
        }
        g_free(argv[i]);argv[i]=g_string_free(value,FALSE);
    }
    if(!image || !*argv[0]) {
        g_strfreev(argv);fail(error,"The emulator command must include {cue} or {iso} for the game image.");return NULL;
    }
    return argv;
}

gboolean project_validate_scene(const Project *p, GError **error)
{
    if(p->emulator_command && *p->emulator_command) {
        char **argv=project_emulator_argv(p->emulator_command,"game.cue","GAME.ISO",error);
        if(!argv) return FALSE;
        g_strfreev(argv);
    }
    if (!p->name || !*p->name || p->objects->len > STUDIO_MAX_OBJECTS)
        return fail(error, "Empty project name or too many objects (maximum 512).");
    for (guint i = 0; i < p->objects->len; i++) {
        Object *o = g_ptr_array_index(p->objects, i);
        if (!o->id || o->id >= G_MAXINT || !o->name || !*o->name || o->kind > OBJECT_CAMERA || o->kind < OBJECT_MESH)
            return fail(error, "Invalid object ID, name or type.");
        for (guint j = 0; j < i; j++)
            if (((Object *)g_ptr_array_index(p->objects, j))->id == o->id)
                return fail(error, "Duplicate object IDs.");
        if (!relative_asset(o->asset) || !relative_asset(o->module) || !relative_asset(o->texture))
            return fail(error, "Assets and modules must use paths relative to the data folder, without '..'.");
        if (o->input_mode < 0 || o->input_mode > 2 || o->collision_action < 0 || o->collision_action > 3 || o->speed < 1 || o->speed > 32)
            return fail(error, "Invalid object behavior.");
        if(o->kind==OBJECT_CAMERA && (*o->asset || *o->texture || o->solid || o->collision_action ||
            !isfinite(o->camera_zoom) || o->camera_zoom<.15 || o->camera_zoom>6 ||
            o->camera_projection<0 || o->camera_projection>1 || o->rotation[0]<-90 || o->rotation[0]>90 || o->rotation[2]!=0))
            return fail(error,"Invalid camera: pitch must be -90 to 90, roll 0; no visual assets or collisions.");
        if(o->kind!=OBJECT_CAMERA && o->follow_target) return fail(error,"Only cameras can follow objects.");
        if(o->follow_target) {
            gboolean found=FALSE;
            for(guint k=0;k<p->objects->len;k++) {Object *target=g_ptr_array_index(p->objects,k);if(target->id==o->follow_target && target!=o && target->kind!=OBJECT_CAMERA) found=TRUE;}
            if(!found) return fail(error,"Invalid camera follow target.");
        }
        for(int k=0;k<3;k++) if(!isfinite(o->collider_offset[k]) || fabs(o->collider_offset[k])>32767 ||
            !isfinite(o->follow_offset[k]) || fabs(o->follow_offset[k])>32767)
            return fail(error,"Invalid collider or follow offset.");
        for (int j = 0; j < 3; j++) {
            if (!isfinite(o->position[j]) || fabs(o->position[j]) > 32767 ||
                !isfinite(o->rotation[j]) || fabs(o->rotation[j]) > 36000 ||
                !isfinite(o->scale[j]) || o->scale[j] <= 0 || o->scale[j] > 128 ||
                !isfinite(o->bounds[j]) || o->bounds[j] <= 0 || o->bounds[j] > 32767)
                return fail(error, "Invalid transforms or values outside project limits.");
        }
    }
    gboolean active=p->active_camera==0;
    for(guint i=0;i<p->objects->len;i++) {
        Object *o=g_ptr_array_index(p->objects,i);
        if(o->id==p->active_camera && o->kind==OBJECT_CAMERA) active=TRUE;
    }
    if(!active || p->drag_axis<0 || p->drag_axis>2) return fail(error,"Invalid active camera or drag plane.");
    for (int j = 0; j < 3; j++)
        if (!isfinite(p->camera[j]) || fabs(p->camera[j]) > 32767)
            return fail(error, "Invalid camera position.");
    if (!isfinite(p->view[0]) || fabs(p->view[0]) > 360 || !isfinite(p->view[1]) || p->view[1] < -90 || p->view[1] > 90 ||
        !isfinite(p->view[2]) || p->view[2] < .15 || p->view[2] > 6) return fail(error, "Invalid camera view.");
    if(p->projection<0 || p->projection>1) return fail(error,"Invalid camera projection.");
    return TRUE;
}

static void string_member(JsonBuilder *b, const char *key, const char *value)
{ json_builder_set_member_name(b, key); json_builder_add_string_value(b, value); }

static void vector_member(JsonBuilder *b, const char *key, const double *v)
{
    json_builder_set_member_name(b, key); json_builder_begin_array(b);
    for (int i = 0; i < 3; i++) json_builder_add_double_value(b, v[i]);
    json_builder_end_array(b);
}

static gboolean copy_data(const char *source, const char *destination, GError **error)
{
    if (!g_file_test(source, G_FILE_TEST_EXISTS)) return TRUE;
    if (g_file_test(source, G_FILE_TEST_IS_SYMLINK)) return fail(error, "The data folder cannot contain symbolic links.");
    if (g_str_has_prefix(destination, source) && (destination[strlen(source)] == '/' || destination[strlen(source)] == 0))
        return fail(error, "The destination cannot be inside the original data folder.");
    GDir *directory = g_dir_open(source, 0, error);
    if (!directory) return FALSE;
    if (g_mkdir_with_parents(destination, 0755) != 0) { g_dir_close(directory); return fail(error, "Cannot copy the data folder."); }
    gboolean ok = TRUE;
    const char *name;
    while (ok && (name = g_dir_read_name(directory))) {
        char *src = g_build_filename(source, name, NULL), *dst = g_build_filename(destination, name, NULL);
        if (g_file_test(src, G_FILE_TEST_IS_SYMLINK) || g_file_test(dst, G_FILE_TEST_IS_SYMLINK)) ok = fail(error, "Symbolic link in the data folder.");
        else if (g_file_test(src, G_FILE_TEST_IS_DIR)) ok = copy_data(src, dst, error);
        else if (g_file_test(dst, G_FILE_TEST_EXISTS)) {
            char *a = NULL, *b = NULL; gsize na, nb;
            ok = g_file_get_contents(src, &a, &na, error) && g_file_get_contents(dst, &b, &nb, error);
            if (ok && (na != nb || memcmp(a, b, na))) ok = fail(error, "The destination contains different data: choose another name.");
            g_free(a); g_free(b);
        } else {
            GFile *from = g_file_new_for_path(src), *to = g_file_new_for_path(dst);
            ok = g_file_copy(from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
            g_object_unref(from); g_object_unref(to);
        }
        g_free(src); g_free(dst);
    }
    g_dir_close(directory); return ok;
}

JsonNode *project_scene_to_json(const Project *p)
{
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "version"); json_builder_add_int_value(b, STUDIO_VERSION);
    string_member(b, "name", p->name);
    string_member(b, "psyq_path", p->psyq_path);
    json_builder_set_member_name(b,"show_colliders");json_builder_add_boolean_value(b,p->show_colliders);
    vector_member(b, "camera", p->camera); vector_member(b, "view", p->view);
    string_member(b,"projection",p->projection?"perspective":"orthographic");
    string_member(b,"emulator_command",p->emulator_command);
    string_member(b, "dosbox", p->dosbox); string_member(b, "output_path", p->output_path);
    json_builder_set_member_name(b,"active_camera");json_builder_add_int_value(b,p->active_camera);
    json_builder_set_member_name(b,"show_game_frame");json_builder_add_boolean_value(b,p->show_game_frame);
    json_builder_set_member_name(b,"drag_axis");json_builder_add_int_value(b,p->drag_axis);
    json_builder_set_member_name(b, "objects"); json_builder_begin_array(b);
    const char *kinds[] = { "mesh", "sprite", "billboard", "camera" };
    for (guint i = 0; i < p->objects->len; i++) {
        Object *o = g_ptr_array_index(p->objects, i);
        json_builder_begin_object(b);
        json_builder_set_member_name(b, "id"); json_builder_add_int_value(b, o->id);
        string_member(b, "name", o->name); string_member(b, "kind", kinds[o->kind]);
        vector_member(b, "position", o->position); vector_member(b, "rotation", o->rotation);
        vector_member(b,"collider_offset",o->collider_offset);vector_member(b,"follow_offset",o->follow_offset);
        json_builder_set_member_name(b,"follow_target");json_builder_add_int_value(b,o->follow_target);
        json_builder_set_member_name(b,"target_scene");json_builder_add_int_value(b,o->target_scene);
        vector_member(b, "scale", o->scale); vector_member(b, "bounds", o->bounds);
        json_builder_set_member_name(b, "solid"); json_builder_add_boolean_value(b, o->solid);
        string_member(b, "asset", o->asset); string_member(b, "module", o->module); string_member(b, "texture", o->texture);
        json_builder_set_member_name(b, "input_mode"); json_builder_add_int_value(b, o->input_mode);
        json_builder_set_member_name(b, "collision_action"); json_builder_add_int_value(b, o->collision_action);
        json_builder_set_member_name(b, "speed"); json_builder_add_int_value(b, o->speed);
        if(o->kind==OBJECT_CAMERA) {
            json_builder_set_member_name(b,"camera_zoom");json_builder_add_double_value(b,o->camera_zoom);
            json_builder_set_member_name(b,"camera_projection");json_builder_add_int_value(b,o->camera_projection);
        }
        json_builder_end_object(b);
    }
    json_builder_end_array(b); json_builder_end_object(b);
    JsonNode *root = json_builder_get_root(b);
    g_object_unref(b);return root;
}


gboolean project_save(Project *p, const char *path, GError **error)
{
    if (!g_str_has_suffix(path, ".psx")) return fail(error, "The project filename must end in .psx.");
    if (!project_validate(p, error)) return FALSE;
    char *canonical = g_canonicalize_filename(path, NULL);
    char *data_dir = project_data_path(canonical);
    if (g_mkdir_with_parents(data_dir, 0755) != 0) {
        g_free(canonical); g_free(data_dir);
        return fail(error, "Cannot create the project data folder.");
    }
    if (p->path && !g_str_equal(p->path, canonical)) {
        for(guint scene=0;scene<p->scenes->len;scene++) {
            Project view;if(!project_scene_view(p,scene,&view,error)) {project_clear(&view);g_free(data_dir);g_free(canonical);return FALSE;}
            gboolean valid=TRUE;
            for(guint i=0;i<view.objects->len && valid;i++) {
                Object *o=g_ptr_array_index(view.objects,i);const char *refs[]={o->asset,o->texture,o->module};
                for(int k=0;k<3;k++) if(*refs[k]) {char *source=asset_path(&view,refs[k]);valid=source && g_file_test(source,G_FILE_TEST_IS_REGULAR);g_free(source);if(!valid) break;}
            }
            project_clear(&view);
            if(!valid) {g_free(data_dir);g_free(canonical);return fail(error,"Save as: a referenced asset is missing from a Scene.");}
        }
        char *old_data = project_data_path(p->path);
        gboolean copied = copy_data(old_data, data_dir, error);
        g_free(old_data);
        if (!copied) { g_free(data_dir); g_free(canonical); return FALSE; }
    }
    g_free(data_dir);
    JsonNode *root=project_to_json(p);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root); json_generator_set_pretty(generator, TRUE);
    gsize size;
    char *text = json_generator_to_data(generator, &size);
    gboolean ok = g_file_set_contents_full(canonical, text, size,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0644, error);
    if (ok) { g_free(p->path); p->path = g_strdup(canonical); p->dirty = FALSE; }
    g_free(canonical); g_free(text); json_node_free(root); g_object_unref(generator);
    return ok;
}

static gboolean typed(JsonObject *o, const char *key, GType type)
{
    JsonNode *n = json_object_get_member(o, key);
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == type;
}

static gboolean vector_read(JsonObject *o, const char *key, double *v)
{
    JsonNode *n = json_object_get_member(o, key);
    if (!n || !JSON_NODE_HOLDS_ARRAY(n)) return FALSE;
    JsonArray *a = json_node_get_array(n);
    if (json_array_get_length(a) != 3) return FALSE;
    for (int i = 0; i < 3; i++) {
        JsonNode *entry = json_array_get_element(a, i);
        GType type = json_node_get_value_type(entry);
        if (!JSON_NODE_HOLDS_VALUE(entry) || (type != G_TYPE_DOUBLE && type != G_TYPE_INT64)) return FALSE;
        v[i] = json_node_get_double(entry);
    }
    return TRUE;
}

gboolean project_scene_decode(Project *p, JsonNode *root, GError **error)
{
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) return fail(error, "The project must be a JSON object.");
    JsonObject *doc = json_node_get_object(root);
    if (!typed(doc, "version", G_TYPE_INT64) || (json_object_get_int_member(doc, "version") < 1 || json_object_get_int_member(doc, "version") > STUDIO_VERSION))
        return fail(error, "Unsupported project version.");
    JsonNode *objects = json_object_get_member(doc, "objects");
    if (!typed(doc, "name", G_TYPE_STRING) || !typed(doc, "psyq_path", G_TYPE_STRING) ||
        !objects || !JSON_NODE_HOLDS_ARRAY(objects) || !vector_read(doc, "camera", p->camera))
        return fail(error, "Incomplete project: missing name, settings, camera or objects.");
    g_free(p->name); p->name = g_strdup(json_object_get_string_member(doc, "name"));
    g_free(p->psyq_path); p->psyq_path = g_strdup(json_object_get_string_member(doc, "psyq_path"));
    if (json_object_has_member(doc,"projection")) {
        if(!typed(doc,"projection",G_TYPE_STRING)) return fail(error,"Invalid camera projection.");
        const char *mode=json_object_get_string_member(doc,"projection");
        if(!g_str_equal(mode,"orthographic") && !g_str_equal(mode,"perspective")) return fail(error,"Unknown camera projection.");
        p->projection=g_str_equal(mode,"perspective");
    }
    if (json_object_has_member(doc, "view") && !vector_read(doc, "view", p->view)) return fail(error, "Invalid camera view.");
    if (json_object_has_member(doc, "dosbox")) {
        if (!typed(doc, "dosbox", G_TYPE_STRING)) return fail(error, "Invalid tool settings.");
        g_free(p->dosbox); p->dosbox = g_strdup(json_object_get_string_member(doc, "dosbox"));
    }
    if(json_object_has_member(doc,"emulator_command")) {
        if(!typed(doc,"emulator_command",G_TYPE_STRING)) return fail(error,"Invalid emulator command.");
        g_free(p->emulator_command);p->emulator_command=g_strdup(json_object_get_string_member(doc,"emulator_command"));
    }
    if (json_object_has_member(doc, "output_path")) {
        if (!typed(doc, "output_path", G_TYPE_STRING)) return fail(error, "Invalid output folder.");
        g_free(p->output_path); p->output_path = g_strdup(json_object_get_string_member(doc, "output_path"));
    }
    if(json_object_has_member(doc,"show_colliders")) {
        if(!typed(doc,"show_colliders",G_TYPE_BOOLEAN)) return fail(error,"Invalid collider visibility.");
        p->show_colliders=json_object_get_boolean_member(doc,"show_colliders");
    }
    guint active_camera=0;
    if(json_object_has_member(doc,"active_camera")) {
        if(!typed(doc,"active_camera",G_TYPE_INT64)) return fail(error,"Invalid active camera.");
        gint64 id=json_object_get_int_member(doc,"active_camera");
        if(id<0 || id>=G_MAXINT) return fail(error,"Invalid active camera.");
        active_camera=id;
    }
    if(json_object_has_member(doc,"show_game_frame")) {
        if(!typed(doc,"show_game_frame",G_TYPE_BOOLEAN)) return fail(error,"Invalid game frame setting.");
        p->show_game_frame=json_object_get_boolean_member(doc,"show_game_frame");
    }
    if(json_object_has_member(doc,"drag_axis")) {
        if(!typed(doc,"drag_axis",G_TYPE_INT64)) return fail(error,"Invalid drag plane.");
        gint64 axis=json_object_get_int_member(doc,"drag_axis");
        if(axis<0 || axis>2) return fail(error,"Invalid drag plane.");
        p->drag_axis=axis;
    }
    JsonArray *a = json_node_get_array(objects);
    if (json_array_get_length(a) > STUDIO_MAX_OBJECTS) return fail(error, "Too many objects.");
    for (guint i = 0; i < json_array_get_length(a); i++) {
        JsonNode *node = json_array_get_element(a, i);
        if (!JSON_NODE_HOLDS_OBJECT(node)) return fail(error, "Invalid object.");
        JsonObject *entry = json_node_get_object(node);
        if (!typed(entry, "id", G_TYPE_INT64) || !typed(entry, "name", G_TYPE_STRING) ||
            !typed(entry, "kind", G_TYPE_STRING) || !typed(entry, "solid", G_TYPE_BOOLEAN) ||
            !typed(entry, "asset", G_TYPE_STRING) || !typed(entry, "module", G_TYPE_STRING))
            return fail(error, "Missing object properties or incorrect property types.");
        gint64 id = json_object_get_int_member(entry, "id");
        const char *kind = json_object_get_string_member(entry, "kind");
        int k = g_str_equal(kind, "mesh") ? 0 : g_str_equal(kind, "sprite") ? 1 : g_str_equal(kind, "billboard") ? 2 : g_str_equal(kind,"camera") ? 3 : -1;
        if (id <= 0 || id >= G_MAXINT || k < 0) return fail(error, "Invalid object type or ID.");
        Object *o = project_add(p, k);
        if (!o) return fail(error, "Object limit reached.");
        o->id = id;
        g_free(o->name); o->name = g_strdup(json_object_get_string_member(entry, "name"));
        g_free(o->asset); o->asset = g_strdup(json_object_get_string_member(entry, "asset"));
        g_free(o->module); o->module = g_strdup(json_object_get_string_member(entry, "module"));
        o->solid = json_object_get_boolean_member(entry, "solid");
        if (json_object_has_member(entry, "texture")) {
            if (!typed(entry, "texture", G_TYPE_STRING)) return fail(error, "Invalid texture.");
            g_free(o->texture); o->texture = g_strdup(json_object_get_string_member(entry, "texture"));
        }
        const char *keys[] = { "input_mode", "collision_action", "speed" };
        int *values[] = { &o->input_mode, &o->collision_action, &o->speed };
        for (int k = 0; k < 3; k++) if (json_object_has_member(entry, keys[k])) {
            if (!typed(entry, keys[k], G_TYPE_INT64)) return fail(error, "Invalid behaviors.");
            gint64 value = json_object_get_int_member(entry, keys[k]);
            if (value < 0 || value > 32) return fail(error, "Behavior value outside allowed limits.");
            *values[k] = value;
        }
        const char *refs[]={"follow_target","target_scene"};guint *ids[]={&o->follow_target,&o->target_scene};
        for(int k=0;k<2;k++) if(json_object_has_member(entry,refs[k])) {
            if(!typed(entry,refs[k],G_TYPE_INT64)) return fail(error,"Invalid object reference.");
            gint64 value=json_object_get_int_member(entry,refs[k]);
            if(value<0 || value>=G_MAXINT) return fail(error,"Invalid object reference.");
            *ids[k]=value;
        }
        if(json_object_has_member(entry,"collider_offset") && !vector_read(entry,"collider_offset",o->collider_offset)) return fail(error,"Invalid collider offset.");
        if(json_object_has_member(entry,"follow_offset") && !vector_read(entry,"follow_offset",o->follow_offset)) return fail(error,"Invalid follow offset.");
        if(o->kind==OBJECT_CAMERA) {
            if(json_object_has_member(entry,"camera_zoom")) {
                if(!typed(entry,"camera_zoom",G_TYPE_DOUBLE) && !typed(entry,"camera_zoom",G_TYPE_INT64)) return fail(error,"Invalid camera zoom.");
                o->camera_zoom=json_object_get_double_member(entry,"camera_zoom");
            }
            if(json_object_has_member(entry,"camera_projection")) {
                if(!typed(entry,"camera_projection",G_TYPE_INT64)) return fail(error,"Invalid camera projection.");
                gint64 mode=json_object_get_int_member(entry,"camera_projection");
                if(mode<0 || mode>1) return fail(error,"Invalid camera projection.");
                o->camera_projection=mode;
            }
        }
        if (!vector_read(entry, "position", o->position) || !vector_read(entry, "rotation", o->rotation) ||
            !vector_read(entry, "scale", o->scale) || !vector_read(entry, "bounds", o->bounds))
            return fail(error, "Missing or invalid object transforms.");
    }
    p->active_camera=active_camera;
    p->next_id = 1;
    for (guint i = 0; i < p->objects->len; i++) {
        Object *o = g_ptr_array_index(p->objects, i);
        p->next_id = MAX(p->next_id, o->id + 1);
    }
    return project_validate_scene(p, error);
}

gboolean project_load(Project *p, const char *path, GError **error)
{
    JsonParser *parser = json_parser_new();
    gboolean ok = json_parser_load_from_file(parser, path, error);
    Project candidate;
    project_init(&candidate);
    if (ok) ok = project_from_json(&candidate, json_parser_get_root(parser), error);
    if (ok) {
        candidate.path = g_canonicalize_filename(path, NULL);
        candidate.dirty = FALSE;
        project_clear(p); *p = candidate;
    } else project_clear(&candidate);
    g_object_unref(parser);
    return ok;
}

static gboolean build_files_exist(const char *directory)
{
    const char *names[]={"SUCCESS.OK","cdrom/game.cue","cdrom/GAME.ISO"};
    for(guint i=0;i<G_N_ELEMENTS(names);i++) {
        char *file=g_build_filename(directory,names[i],NULL);
        gboolean exists=g_file_test(file,G_FILE_TEST_IS_REGULAR);g_free(file);
        if(!exists) return FALSE;
    }
    return TRUE;
}

static gboolean build_matches_project(const char *directory,const Project *p)
{
    char *marker=g_build_filename(directory,".psx-studio-build",NULL),*project=NULL;
    gboolean same=FALSE;
    if(g_file_get_contents(marker,&project,NULL,NULL)) {
        GStatBuf a,b;
        same=g_str_equal(project,p->path) ||
            (g_stat(project,&a)==0 && g_stat(p->path,&b)==0 && a.st_dev==b.st_dev && a.st_ino==b.st_ino);
    }
    g_free(marker);g_free(project);
    return same && build_files_exist(directory);
}

char *project_build_output(const Project *p, const char *root)
{
    if (!p->path) {
        char *marker=g_build_filename(root,"build","last-ps1-build.txt",NULL), *directory=NULL;
        g_file_get_contents(marker,&directory,NULL,NULL);g_free(marker);
        if(directory) g_strstrip(directory);
        if(directory && build_files_exist(directory)) return directory;
        g_free(directory);return NULL;
    }
    char *parent=g_path_get_dirname(p->path);
    char *base=*p->output_path?g_canonicalize_filename(p->output_path,parent):g_build_filename(root,"build","ps1",NULL);
    g_free(parent);
    /* Prefer the current fixed folder over older NAME-HASH builds. */
    char *stem=g_path_get_basename(p->path);
    char *extension=strrchr(stem,'.');if(extension && extension!=stem) *extension=0;
    GRegex *pattern=g_regex_new("[^a-zA-Z0-9_-]",0,0,NULL);
    char *safe=g_regex_replace_literal(pattern,stem,-1,0,"_",0,NULL);
    char *slug=g_strndup(safe,32);
    g_regex_unref(pattern);g_free(safe);g_free(stem);
    if(!*p->output_path) {
        char *fixed=g_build_filename(base,slug,NULL);
        if(build_matches_project(fixed,p)) {g_free(slug);g_free(base);return fixed;}
        g_free(fixed);
    }
    g_free(slug);
    GDir *dir=g_dir_open(base,0,NULL);char *found=NULL;
    if(dir) {
        const char *name;
        while((name=g_dir_read_name(dir))) {
            char *directory=g_build_filename(base,name,NULL);
            if(build_matches_project(directory,p)) found=g_strdup(directory);
            g_free(directory);
            if(found) break;
        }
        g_dir_close(dir);
    }
    g_free(base);return found;
}

char *project_build_sdk(const Project *p,const char *root)
{
    char *directory=project_build_output(p,root);
    if(!directory) return NULL;
    char *file=g_build_filename(directory,"build-info.json",NULL), *sdk=NULL;
    JsonParser *parser=json_parser_new();
    if(json_parser_load_from_file(parser,file,NULL)) {
        JsonNode *node=json_parser_get_root(parser);
        if(node && JSON_NODE_HOLDS_OBJECT(node)) {
            JsonObject *info=json_node_get_object(node);
            if(typed(info,"psyq_path",G_TYPE_STRING)) {
                const char *path=json_object_get_string_member(info,"psyq_path");
                if(path && *path) sdk=g_strdup(path);
            }
        }
    }
    g_object_unref(parser);g_free(file);
    /* Older builds already record the effective SDK in their DOSBox mount. */
    if(!sdk) {
        file=g_build_filename(directory,"dos.conf",NULL);char *contents=NULL;
        if(g_file_get_contents(file,&contents,NULL,NULL)) {
            char **lines=g_strsplit(contents,"\n",-1);
            for(int i=0;lines[i] && !sdk;i++) {
                char *line=g_strstrip(lines[i]);
                if(g_str_has_prefix(line,"mount d \"")) {
                    char *end=strstr(line+9,"\" -ro");
                    if(end && end>line+9) sdk=g_strndup(line+9,end-line-9);
                }
            }
            g_strfreev(lines);g_free(contents);
        }
        g_free(file);
    }
    g_free(directory);return sdk;
}
