#include "studio.h"
#include "audio.h"
#include <string.h>
#include <math.h>

static Studio studio;
static int exit_status;
static gboolean smoke_snapshot(gpointer data);
static void refresh_output(Studio *s);
static GtkWidget *create_titlebar(GtkWindow *window,const char *text,gboolean full_controls);

static void menu_enabled(Studio *s,const char *name,gboolean enabled)
{
    GAction *action=g_action_map_lookup_action(G_ACTION_MAP(s->window),name);
    if(action) g_simple_action_set_enabled(G_SIMPLE_ACTION(action),enabled);
}

static Object *selected(Studio *s)
{
    return s->selected >= 0 && (guint)s->selected < s->project.objects->len ?
        g_ptr_array_index(s->project.objects, s->selected) : NULL;
}

void studio_status(Studio *s, const char *message)
{
    gtk_label_set_text(GTK_LABEL(s->status), message);
    if(s->settings_status && gtk_widget_get_visible(s->settings_window))
        gtk_label_set_text(GTK_LABEL(s->settings_status),message);
    if (s->log) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->log));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, message, -1);
        gtk_text_buffer_insert(buffer, &end, "\n", 1);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(s->log), &end, 0, FALSE, 0, 0);
    }
}

static void title(Studio *s)
{
    char *text = g_strdup_printf("%s%s - PSX Studio", s->project.dirty ? "* " : "", s->project.name);
    gtk_window_set_title(GTK_WINDOW(s->window), text);
    if (s->title_label) gtk_label_set_text(GTK_LABEL(s->title_label), text);
    g_free(text);
    refresh_output(s);
}

static JsonNode *history_snapshot(Studio *s)
{
    JsonNode *node=project_to_json(&s->project);Object *o=selected(s);
    json_object_set_int_member(json_node_get_object(node),"_selected_object",o?o->id:0);
    char *text=json_to_string(node,FALSE);json_object_set_int_member(json_node_get_object(node),"_history_size",strlen(text));g_free(text);return node;
}
static void trim_history(Studio *s)
{
    gsize bytes=0;GPtrArray *stacks[]={s->undo,s->redo};
    for(int k=0;k<2;k++) for(guint i=0;i<stacks[k]->len;i++) bytes+=json_object_get_int_member(json_node_get_object(g_ptr_array_index(stacks[k],i)),"_history_size");
    while(bytes>8*1024*1024 || s->undo->len+s->redo->len>64) {
        GPtrArray *old=s->undo->len>1?s->undo:s->redo->len>1?s->redo:s->undo->len && s->redo->len?s->undo:NULL;if(!old) break;
        bytes-=json_object_get_int_member(json_node_get_object(g_ptr_array_index(old,0)),"_history_size");g_ptr_array_remove_index(old,0);
    }
}
void studio_checkpoint(Studio *s,gpointer key)
{
    if(s->refreshing || s->restoring || !s->undo || s->build_process) return;
    gint64 now=g_get_monotonic_time();
    if(key && key==s->history_key && now-s->history_time<600000) {s->history_time=now;return;}
    g_ptr_array_add(s->undo,history_snapshot(s));
    g_ptr_array_set_size(s->redo,0);trim_history(s);s->history_key=key;s->history_time=now;
    menu_enabled(s,"undo",TRUE);
    menu_enabled(s,"redo",FALSE);
}
static void history_reset(Studio *s)
{g_ptr_array_set_size(s->undo,0);g_ptr_array_set_size(s->redo,0);s->history_key=NULL;}
static void history_action(gboolean redo,Studio *s)
{
    if(s->build_process) return;
    GPtrArray *from=redo?s->redo:s->undo,*to=redo?s->undo:s->redo;
    if(!from->len) return;
    Project restored;project_init(&restored);GError *error=NULL;
    if(!project_from_json(&restored,g_ptr_array_index(from,from->len-1),&error)) {
        studio_status(s,error->message);g_error_free(error);project_clear(&restored);return;
    }
    JsonNode *snapshot=g_ptr_array_index(from,from->len-1);guint selected_id=0;
    JsonObject *doc=json_node_get_object(snapshot);if(json_object_has_member(doc,"_selected_object")) selected_id=json_object_get_int_member(doc,"_selected_object");
    g_ptr_array_add(to,history_snapshot(s));g_ptr_array_remove_index(from,from->len-1);trim_history(s);
    restored.path=g_strdup(s->project.path);restored.asset_root=g_strdup(s->project.asset_root);restored.dirty=TRUE;
    viewport_clear_assets(s);project_clear(&s->project);s->project=restored;s->selected=-1;s->dragging=FALSE;
    for(guint i=0;i<restored.objects->len;i++) if(((Object*)g_ptr_array_index(restored.objects,i))->id==selected_id) s->selected=i;
    s->history_key=NULL;s->restoring=TRUE;studio_refresh(s);s->restoring=FALSE;
    studio_status(s,redo?"Redo applied.":"Undo applied.");
}

static void update_dropdown_model(GtkWidget *widget,GtkStringList *names)
{
    GListModel *old=gtk_drop_down_get_model(GTK_DROP_DOWN(widget));
    guint count=g_list_model_get_n_items(G_LIST_MODEL(names));gboolean same=old && GTK_IS_STRING_LIST(old) && g_list_model_get_n_items(old)==count;
    for(guint i=0;same && i<count;i++) same=g_strcmp0(gtk_string_list_get_string(GTK_STRING_LIST(old),i),gtk_string_list_get_string(names,i))==0;
    /* Replacing a model from its selection callback can re-enter GTK's popup. */
    if(!same) gtk_drop_down_set_model(GTK_DROP_DOWN(widget),G_LIST_MODEL(names));
}

void studio_refresh(Studio *s)
{
    s->refreshing = TRUE;
    if(s->scene_selector) {
        GtkStringList *names=gtk_string_list_new(NULL);guint index=0;
        for(guint i=0;i<s->project.scenes->len;i++) {Scene *scene=g_ptr_array_index(s->project.scenes,i);
            char *label=g_strdup_printf("%u: %s%s",scene->id,scene->name,scene->id==s->project.startup_scene?" [start]":"");
            gtk_string_list_append(names,label);g_free(label);if(scene->id==s->project.scene_id) index=i;}
        update_dropdown_model(s->scene_selector,names);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s->scene_selector),index);g_object_unref(names);
        gtk_editable_set_text(GTK_EDITABLE(s->scene_name),project_scene_find(&s->project,s->project.scene_id)->name);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(s->startup_button),s->project.startup_scene==s->project.scene_id);
    }
    menu_enabled(s,"undo",s->undo && s->undo->len);
    menu_enabled(s,"redo",s->redo && s->redo->len);
    GtkWidget *row;
    while ((row = gtk_widget_get_first_child(s->list))) gtk_list_box_remove(GTK_LIST_BOX(s->list), row);
    const char *kinds[] = { "3D", "2D", "BB", "CAM" };
    for (guint i = 0; i < s->project.objects->len; i++) {
        Object *o = g_ptr_array_index(s->project.objects, i);
        char *text = g_strdup_printf("[%s]  %s", kinds[o->kind], o->name);
        GtkWidget *label = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(label), 0);gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_start(label, 8); gtk_widget_set_margin_end(label, 8);
        gtk_widget_set_margin_top(label, 6); gtk_widget_set_margin_bottom(label, 6);
        gtk_list_box_append(GTK_LIST_BOX(s->list), label); g_free(text);
    }
    if (s->selected >= 0) gtk_list_box_select_row(GTK_LIST_BOX(s->list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(s->list), s->selected));
    Object *o = selected(s);
    gtk_widget_set_sensitive(s->name, o != NULL); gtk_widget_set_sensitive(s->kind, o != NULL);
    gtk_widget_set_sensitive(s->solid, o != NULL);
    gtk_editable_set_text(GTK_EDITABLE(s->name), o ? o->name : "");
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s->kind), o ? o->kind : 0);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(s->solid), o && o->solid);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(s->numbers[1][0]),o && o->kind==OBJECT_CAMERA?-90:-36000,o && o->kind==OBJECT_CAMERA?90:36000);
    double effective[3]={0};if(o) project_camera_position(&s->project,o,effective);
    for (int group = 0; group < 4; group++) for (int axis = 0; axis < 3; axis++) {
        double value = o ? (group == 0 ? effective[axis] : group == 1 ? o->rotation[axis] :
            group == 2 ? o->scale[axis] : o->bounds[axis]) : 0;
        gtk_widget_set_sensitive(s->numbers[group][axis], o != NULL);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->numbers[group][axis]), value);
    }
    gtk_editable_set_text(GTK_EDITABLE(s->psyq), s->project.psyq_path);
    gtk_editable_set_text(GTK_EDITABLE(s->project_name), s->project.name);
    if(s->texture_label) {
        gtk_label_set_text(GTK_LABEL(s->asset_label),o && *o->asset?o->asset:"No model");
        gtk_label_set_text(GTK_LABEL(s->texture_label),o && *o->texture?o->texture:"No texture");
        gtk_label_set_text(GTK_LABEL(s->module_label),o && *o->module?o->module:"No module");
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s->input_mode),o?o->input_mode:0);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s->collision_action),o?o->collision_action:0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->speed),o?o->speed:2);
        gtk_widget_set_sensitive(s->input_mode,o!=NULL);gtk_widget_set_sensitive(s->collision_action,o!=NULL);gtk_widget_set_sensitive(s->speed,o!=NULL);
        for(int k=0;k<6;k++) gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->camera_values[k]),k<3?s->project.camera[k]+(k==2?600:0):s->project.view[k-3]);
        gtk_editable_set_text(GTK_EDITABLE(s->dosbox),s->project.dosbox);
        gtk_editable_set_text(GTK_EDITABLE(s->emulator_entry),s->project.emulator_command);
    }
    if(s->camera_panel) {
        gboolean cam=o && o->kind==OBJECT_CAMERA;
        gtk_widget_set_visible(s->camera_panel,cam);
        gtk_widget_set_visible(s->scale_panel,!cam);
        gtk_widget_set_sensitive(s->input_mode,o && !cam);
        gtk_widget_set_sensitive(s->speed,o && !cam && o->input_mode!=0);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(s->camera_active),cam && s->project.active_camera==o->id);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->camera_zoom),cam?o->camera_zoom:1);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s->camera_projection),cam?o->camera_projection:0);
        gtk_widget_set_sensitive(s->solid,o && !cam);
        gtk_widget_set_sensitive(s->collision_action,o && !cam);
        for(int k=0;k<2;k++) gtk_widget_set_sensitive(s->visual_imports[k],o && !cam);
        for(int g=2;g<4;g++) for(int k=0;k<3;k++) gtk_widget_set_sensitive(s->numbers[g][k],o && !cam);
        for(int k=0;k<3;k++) gtk_widget_set_sensitive(s->numbers[0][k],o && !(cam && o->follow_target));
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(s->numbers[1][0]),cam?-90:-36000,cam?90:36000);
        gtk_widget_set_sensitive(s->numbers[1][2],o && !cam);
    }
    if(s->collision_scene) {
        GtkStringList *names=gtk_string_list_new(NULL);gtk_string_list_append(names,"No destination");guint index=0;
        for(guint i=0;i<s->project.scenes->len;i++) {Scene *scene=g_ptr_array_index(s->project.scenes,i);
            char *label=g_strdup_printf("%u: %s",scene->id,scene->name);gtk_string_list_append(names,label);g_free(label);
            if(o && o->target_scene==scene->id) index=i+1;}
        update_dropdown_model(s->collision_scene,names);g_object_unref(names);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s->collision_scene),index);gtk_widget_set_sensitive(s->collision_scene,o && o->kind!=OBJECT_CAMERA && o->collision_action==3);
        GtkStringList *targets=gtk_string_list_new(NULL);gtk_string_list_append(targets,"No follow target");index=0;
        for(guint i=0;i<s->project.objects->len;i++) {Object *target=g_ptr_array_index(s->project.objects,i);
            char *label=g_strdup_printf("%u: %s",target->id,target->name);gtk_string_list_append(targets,label);g_free(label);
            if(o && o->follow_target==target->id) index=i+1;}
        update_dropdown_model(s->follow_target,targets);g_object_unref(targets);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s->follow_target),index);
        for(int k=0;k<3;k++) {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->collider_offset[k]),o?o->collider_offset[k]:0);
            gtk_widget_set_sensitive(s->collider_offset[k],o && o->kind!=OBJECT_CAMERA);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->follow_offset[k]),o?o->follow_offset[k]:0);
            gtk_widget_set_sensitive(s->follow_offset[k],o && o->kind==OBJECT_CAMERA && o->follow_target!=0);
        }
    }
    if(s->colliders_button) gtk_check_button_set_active(GTK_CHECK_BUTTON(s->colliders_button),s->project.show_colliders);
    if(s->frame_button) gtk_check_button_set_active(GTK_CHECK_BUTTON(s->frame_button),s->project.show_game_frame);
    if(s->drag_plane) gtk_drop_down_set_selected(GTK_DROP_DOWN(s->drag_plane),s->project.drag_axis);
    if(s->drag_hint) gtk_label_set_text(GTK_LABEL(s->drag_hint),s->project.drag_axis==1?
        "Left drag: X/Z | Right drag: orbit | Wheel: zoom | Shift+wheel: pan":s->project.drag_axis==2?
        "Left drag: X/Y | Right drag: orbit | Wheel: zoom | Shift+wheel: pan":"Left drag: Y/Z | Right drag: orbit | Wheel: zoom | Shift+wheel: pan");
    if(s->view_preset) {
        double yaw=s->project.view[0],pitch=s->project.view[1];
        int preset=pitch==90?1:pitch==-90?2:pitch!=0?0:yaw==0?3:fabs(yaw)==180?4:yaw==-90?5:yaw==90?6:0;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(s->view_preset),preset);
    }
    if(s->projection_button) gtk_button_set_label(GTK_BUTTON(s->projection_button),
        s->project.projection?"View: Perspective (P)":"View: Orthographic (P)");
    title(s);
    gtk_gl_area_queue_render(GTK_GL_AREA(s->viewport));
    s->refreshing = FALSE;
}

static void row_selected(GtkListBox *list, GtkListBoxRow *row, Studio *s)
{
    (void)list;
    if (s->refreshing) return;
    s->history_key=NULL;
    s->selected = row ? gtk_list_box_row_get_index(row) : -1;
    studio_refresh(s);
}

static void property_changed(GtkWidget *widget, Studio *s)
{
    if (s->refreshing) return;
    Object *o = selected(s);
    if (!o) return;
    studio_checkpoint(s,widget);
    if (widget == s->name) {
        g_free(o->name); o->name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(widget)));
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(s->list), s->selected);
        const char *kinds[] = { "3D", "2D", "BB", "CAM" };
        char *text = g_strdup_printf("[%s]  %s", kinds[o->kind], o->name);
        gtk_label_set_text(GTK_LABEL(gtk_list_box_row_get_child(row)), text); g_free(text);
    } else if (widget == s->solid) o->solid = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
    else for (int group = 0; group < 4; group++) for (int axis = 0; axis < 3; axis++)
        if (widget == s->numbers[group][axis]) {
            double *values = group == 0 ? o->position : group == 1 ? o->rotation : group == 2 ? o->scale : o->bounds;
            values[axis] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
        }
    s->project.dirty = TRUE; title(s); gtk_gl_area_queue_render(GTK_GL_AREA(s->viewport));
}

static void kind_changed(GObject *widget, GParamSpec *property, Studio *s)
{
    (void)widget; (void)property;
    if (s->refreshing || !selected(s)) return;
    studio_checkpoint(s,NULL);viewport_clear_assets(s);
    Object *o=selected(s);
    o->kind = gtk_drop_down_get_selected(GTK_DROP_DOWN(s->kind));
    if(o->kind==OBJECT_CAMERA) {
        for(guint i=0;i<s->project.objects->len;i++) {Object *camera=g_ptr_array_index(s->project.objects,i);if(camera->follow_target==o->id) camera->follow_target=0;}
        project_camera_from_view(&s->project,o);
        g_free(o->asset);o->asset=g_strdup("");g_free(o->texture);o->texture=g_strdup("");
        o->solid=FALSE;o->collision_action=0;
        if(!s->project.active_camera) s->project.active_camera=o->id;
    } else {o->follow_target=0;if(s->project.active_camera==o->id) s->project.active_camera=0;}
    s->project.dirty = TRUE; studio_refresh(s);
}

static void add_object(GtkButton *button, Studio *s)
{
    int kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "kind"));
    studio_checkpoint(s,NULL);
    Object *o = project_add(&s->project, kind);
    if (!o) { studio_status(s, "The limit of 512 objects has been reached."); return; }
    s->selected = s->project.objects->len - 1;
    studio_refresh(s); studio_status(s, "Object added. Drag it in the viewport on the selected plane.");
}

static void duplicate_object(GtkButton *button, Studio *s)
{
    (void)button;
    if (!selected(s)) return;
    studio_checkpoint(s,NULL);
    if (!project_duplicate(&s->project, s->selected)) return;
    s->selected = s->project.objects->len - 1; studio_refresh(s);
}

static void delete_object(GtkButton *button, Studio *s)
{
    (void)button;
    if (!selected(s)) return;
    studio_checkpoint(s,NULL);viewport_clear_assets(s);
    for(guint i=0;i<s->project.objects->len;i++) {Object *camera=g_ptr_array_index(s->project.objects,i);if(camera->follow_target==selected(s)->id) {double position[3];project_camera_position(&s->project,camera,position);memcpy(camera->position,position,sizeof(position));camera->follow_target=0;}}
    if(s->project.active_camera==selected(s)->id) s->project.active_camera=0;
    g_ptr_array_remove_index(s->project.objects, s->selected);
    s->selected = -1; s->project.dirty = TRUE; studio_refresh(s);
}

static void file_response(GObject *source, GAsyncResult *result, gpointer data)
{
    Studio *s = data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    gboolean saving = GPOINTER_TO_INT(g_object_get_data(source, "saving"));
    GError *dialog_error = NULL;
    GFile *file = saving ? gtk_file_dialog_save_finish(dialog, result, &dialog_error) :
        gtk_file_dialog_open_finish(dialog, result, &dialog_error);
    if (file) {
        char *path = g_file_get_path(file);
        GError *error = NULL;
        if(!saving) viewport_clear_assets(s);
        gboolean ok = path && (saving ? project_save(&s->project, path, &error) : project_load(&s->project, path, &error));
        if (ok) {
            if(!saving) history_reset(s);
            s->selected = s->project.objects->len ? 0 : -1; studio_refresh(s);
            studio_status(s, saving ? "Project saved." : "Project opened.");
            if (!saving) {
                char *dir = project_data_path(path);
                for (guint i = 0; i < s->project.objects->len; i++) {
                    Object *o = g_ptr_array_index(s->project.objects, i);
                    const char *refs[] = { o->asset, o->module, o->texture };
                    for (int j = 0; j < 3; j++) if (*refs[j]) {
                        char *asset = g_build_filename(dir, refs[j], NULL);
                        if (!g_file_test(asset, G_FILE_TEST_IS_REGULAR)) {
                            char *message = g_strdup_printf("Missing asset: %s", asset);
                            studio_status(s, message); g_free(message);
                        }
                        g_free(asset);
                    }
                }
                g_free(dir);
            }
        } else studio_status(s, error ? error->message : "Choose a local file.");
        g_clear_error(&error); g_free(path); g_object_unref(file);
    } else if (dialog_error && !g_error_matches(dialog_error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
        studio_status(s, dialog_error->message);
    g_clear_error(&dialog_error);
}

static void choose_file(Studio *s, gboolean save)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, save ? "Save project" : "Open project");
    gtk_file_dialog_set_modal(dialog, TRUE);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PSX Studio projects (*.psx)"); gtk_file_filter_add_pattern(filter, "*.psx");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter); gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, filter); g_object_unref(filter); g_object_unref(filters);
    g_object_set_data(G_OBJECT(dialog), "saving", GINT_TO_POINTER(save));
    if (save) {
        gtk_file_dialog_set_initial_name(dialog, "project.psx");
        gtk_file_dialog_save(dialog, GTK_WINDOW(s->window), NULL, file_response, s);
    } else gtk_file_dialog_open(dialog, GTK_WINDOW(s->window), NULL, file_response, s);
    g_object_unref(dialog);
}

static void perform_action(Studio *s, int action)
{
    if (action == 0) {
        history_reset(s);viewport_clear_assets(s); project_clear(&s->project); project_init(&s->project); s->selected = -1;
        studio_refresh(s); studio_status(s, "New project. Add an object to get started.");
    } else if (action == 1) choose_file(s, FALSE);
    else gtk_window_destroy(GTK_WINDOW(s->window));
}

typedef struct { Studio *studio; int action; } PendingAction;
static void discard_response(GObject *dialog, GAsyncResult *result, gpointer data)
{
    PendingAction *pending = data;
    GError *error = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(dialog), result, &error);
    if (choice == 1) perform_action(pending->studio, pending->action);
    g_clear_error(&error); g_free(pending);
}

static void request_action(Studio *s, int action)
{
    if (s->build_process) { studio_status(s, "Build in progress: wait for the result before changing projects or closing."); return; }
    if (!s->project.dirty) { perform_action(s, action); return; }
    GtkAlertDialog *dialog = gtk_alert_dialog_new("There are unsaved changes.");
    gtk_alert_dialog_set_detail(dialog, "To keep them, cancel and save the project before continuing.");
    const char *buttons[] = { "Cancel", "Discard changes", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons); gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    PendingAction *pending = g_new(PendingAction, 1); pending->studio = s; pending->action = action;
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(s->window), NULL, discard_response, pending);
    g_object_unref(dialog);
}

static void new_clicked(GtkButton *button, Studio *s) { (void)button; request_action(s, 0); }
static void open_clicked(GtkButton *button, Studio *s) { (void)button; request_action(s, 1); }
static gboolean close_requested(GtkWindow *window, Studio *s) { (void)window; request_action(s, 2); return TRUE; }

static void save_clicked(GtkButton *button, Studio *s)
{
    (void)button;
    if (!s->project.path) { choose_file(s, TRUE); return; }
    GError *error = NULL;
    if (project_save(&s->project, s->project.path, &error)) { title(s); studio_status(s, "Project saved."); }
    else { studio_status(s, error->message); g_error_free(error); }
}

static void save_as_clicked(GtkButton *button, Studio *s) { (void)button; choose_file(s, TRUE); }

static gboolean configure(GtkButton *button, Studio *s)
{
    (void)button;
    const char *path = gtk_editable_get_text(GTK_EDITABLE(s->psyq));
    const char *required[] = { "psyq/CCPSX.EXE", "include/LIBGPU.H", "lib" };
    /* Some SDK installations use lower-case names on the host. */
    for (guint i = 0; i < G_N_ELEMENTS(required); i++) {
        char *file = g_build_filename(path, required[i], NULL);
        char *lower = g_ascii_strdown(required[i], -1);
        char *alternative = g_build_filename(path, lower, NULL);
        gboolean exists = g_file_test(file, i == 2 ? G_FILE_TEST_IS_DIR : G_FILE_TEST_IS_REGULAR) ||
            g_file_test(alternative, i == 2 ? G_FILE_TEST_IS_DIR : G_FILE_TEST_IS_REGULAR);
        g_free(lower); g_free(alternative); g_free(file);
        if (!exists) {
            char *message = g_strdup_printf("Invalid Psy-Q SDK: %s is missing from the selected folder.", required[i]);
            studio_status(s, message); g_free(message); return FALSE;
        }
    }
    char *resolved = g_canonicalize_filename(path, NULL);
    g_free(s->project.psyq_path); s->project.psyq_path = resolved;
    s->project.dirty = TRUE; title(s); studio_status(s, "Psy-Q path verified and applied to the project.");
    return TRUE;
}

static void project_name_changed(GtkEditable *entry, Studio *s)
{
    if (s->refreshing) return;
    studio_checkpoint(s,entry);
    g_free(s->project.name); s->project.name = g_strdup(gtk_editable_get_text(entry));
    s->project.dirty = TRUE; title(s);
}

static void build_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    Studio *s = data;
    char *output = NULL;
    GError *error = NULL;
    gboolean communicated = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &output, NULL, &error);
    if (output && *output) studio_status(s, output);
    gboolean ok = communicated && g_subprocess_get_successful(G_SUBPROCESS(source));
    studio_status(s, error ? error->message : ok ? "PS1 build completed. Use the Build menu to run the emulator or open the output folder." : "PS1 build failed. Check the log.");
    g_clear_error(&error); g_free(output); g_clear_object(&s->build_process);
    gtk_widget_set_sensitive(s->menubar, TRUE); gtk_widget_set_sensitive(s->body, TRUE);
    refresh_output(s);
    if (s->build_and_exit) {
        if (!ok) exit_status = 1;
        if (s->smoke_path) g_timeout_add(300, smoke_snapshot, s);
        else gtk_window_destroy(GTK_WINDOW(s->window));
    }
    g_application_release(G_APPLICATION(s->application));
}

static void build_clicked(GtkButton *button, Studio *s)
{
    (void)button;
    if (s->build_process) return;
    GError *error = NULL;
    const char *problem = NULL;
    if (!s->project.path) problem = "Save the .psx project before building.";
    else if (!*s->project.psyq_path) problem = "Configure and apply the Psy-Q path before building.";
    else {
        /* Editor builds always use the standard build/ps1 project folder. */
        g_free(s->project.output_path);s->project.output_path=g_strdup("");
        if(!project_save(&s->project, s->project.path, &error)) problem=error->message;
    }
    if (problem) {
        studio_status(s, problem); g_clear_error(&error);
        if (s->build_and_exit) { exit_status = 1; gtk_window_destroy(GTK_WINDOW(s->window)); }
        return;
    }
    title(s);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, s->root);
    s->build_process = g_subprocess_launcher_spawn(launcher, &error, s->executable, "--build-ps1",
        "--project", s->project.path, "--psyq", s->project.psyq_path, NULL);
    g_object_unref(launcher);
    if (!s->build_process) {
        studio_status(s, error->message); g_clear_error(&error);
        if (s->build_and_exit) { exit_status = 1; gtk_window_destroy(GTK_WINDOW(s->window)); }
        return;
    }
    gtk_widget_set_sensitive(s->menubar, FALSE); gtk_widget_set_sensitive(s->body, FALSE);
    refresh_output(s);
    studio_status(s, "Project saved. Export and PS1 build in progress; the full log will appear when finished.");
    g_application_hold(G_APPLICATION(s->application));
    g_subprocess_communicate_utf8_async(s->build_process, NULL, NULL, build_finished, s);
}

static GtkWidget *button(GtkWidget *box, const char *label, GCallback callback, Studio *s)
{
    GtkWidget *widget = gtk_button_new_with_label(label);
    gtk_box_append(GTK_BOX(box), widget); g_signal_connect(widget, "clicked", callback, s); return widget;
}

static GtkWidget *heading(const char *text)
{
    GtkWidget *label = gtk_label_new(text); gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_add_css_class(label, "heading95"); return label;
}

static GtkWidget *panel(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 8); gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8); gtk_widget_set_margin_bottom(box, 8);
    return box;
}

static void behavior_changed(GtkWidget *widget,Studio *s)
{
    if(s->refreshing || !selected(s)) return;
    studio_checkpoint(s,widget);
    if(widget==s->speed) selected(s)->speed=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
    s->project.dirty=TRUE;title(s);
}
static void behavior_selected(GObject *widget,GParamSpec *property,Studio *s)
{
    (void)property;
    if(s->refreshing || !selected(s)) return;
    studio_checkpoint(s,widget);
    if(widget==G_OBJECT(s->input_mode)) selected(s)->input_mode=gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
    else {
        selected(s)->collision_action=gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
        if(selected(s)->collision_action==3 && !selected(s)->target_scene) selected(s)->target_scene=s->project.startup_scene;
    }
    s->project.dirty=TRUE;studio_refresh(s);
}
static void toggle_projection(GtkButton *button,Studio *s)
{
    (void)button;
    if(s->build_process) return;
    studio_checkpoint(s,NULL);
    s->dragging=FALSE;
    s->project.projection=!s->project.projection;
    s->project.dirty=TRUE;studio_refresh(s);
    gtk_widget_grab_focus(s->viewport);
}

static gboolean projection_key(GtkEventControllerKey *controller,guint keyval,guint keycode,
                               GdkModifierType state,Studio *s)
{
    (void)controller;(void)keycode;
    GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(s->window));
    if(focus && (GTK_IS_EDITABLE(focus) || GTK_IS_TEXT_VIEW(focus))) return FALSE;
    if((state&GDK_CONTROL_MASK) && (keyval==GDK_KEY_z || keyval==GDK_KEY_Z || keyval==GDK_KEY_y || keyval==GDK_KEY_Y)) {
        history_action(keyval==GDK_KEY_y || keyval==GDK_KEY_Y || (state&GDK_SHIFT_MASK),s);return TRUE;
    }
    if((keyval==GDK_KEY_p || keyval==GDK_KEY_P) &&
        !(state&(GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK))) {
        toggle_projection(NULL,s);return TRUE;
    }
    return FALSE;
}

static void camera_changed(GtkSpinButton *spin,Studio *s)
{
    if(s->refreshing) return;
    studio_checkpoint(s,spin);
    for(int k=0;k<6;k++) if(GTK_WIDGET(spin)==s->camera_values[k]) {
        double v=gtk_spin_button_get_value(spin);
        if(k<3) s->project.camera[k]=v-(k==2?600:0);else s->project.view[k-3]=v;
    }
    s->project.dirty=TRUE;title(s);gtk_gl_area_queue_render(GTK_GL_AREA(s->viewport));
}
typedef struct { char *source,*directory; } AudioImport;
static void audio_import_free(gpointer data)
{
    AudioImport *job=data;g_free(job->source);g_free(job->directory);g_free(job);
}
static void audio_import_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
    (void)source;(void)cancel;AudioImport *job=data;GError *error=NULL;
    char *path=audio_import_wav(job->source,job->directory,&error);
    if(path) g_task_return_pointer(task,path,g_free);else g_task_return_error(task,error);
}
static void audio_import_done(GObject *source,GAsyncResult *result,gpointer data)
{
    (void)source;Studio *s=data;GError *error=NULL;
    char *path=g_task_propagate_pointer(G_TASK(result),&error);
    char *message=path?g_strdup_printf("WAV imported as VAG: %s",path):g_strdup_printf("WAV import failed: %s",error->message);
    gboolean visible=gtk_application_get_active_window(s->application)!=NULL;
    if(visible) studio_status(s,message);
    if(error && visible) {
        GtkAlertDialog *alert=gtk_alert_dialog_new("WAV import failed");
        gtk_alert_dialog_set_detail(alert,error->message);gtk_alert_dialog_show(alert,GTK_WINDOW(s->window));g_object_unref(alert);
    }
    g_free(message);g_free(path);g_clear_error(&error);
    g_application_release(G_APPLICATION(s->application));
}
static void imported(GObject *source,GAsyncResult *result,gpointer data)
{
    Studio *s=data;GError *error=NULL;
    GFile *file=gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source),result,&error);
    if(file) {
        char *path=g_file_get_path(file);int type=GPOINTER_TO_INT(g_object_get_data(source,"import-type"));
        if(type==3 && path) {
            AudioImport *job=g_new0(AudioImport,1);job->source=path;
            job->directory=g_strdup(g_object_get_data(source,"audio-directory"));
            GTask *task=g_task_new(NULL,NULL,audio_import_done,s);
            g_task_set_task_data(task,job,audio_import_free);
            g_application_hold(G_APPLICATION(s->application));
            studio_status(s,"Importing WAV: converting to VAG…");
            g_task_run_in_thread(task,audio_import_worker);g_object_unref(task);
            g_object_unref(file);g_clear_error(&error);return;
        }
        Object *o=selected(s);studio_checkpoint(s,NULL);viewport_clear_assets(s);
        if(o && path && asset_import(&s->project,o,path,type,&error)) {studio_refresh(s);studio_status(s,"Asset imported into the project data folder.");}
        else if(error) studio_status(s,error->message);
        g_free(path);g_object_unref(file);
    } else if(error && !g_error_matches(error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_DISMISSED)) studio_status(s,error->message);
    g_clear_error(&error);
}
static void import_asset(Studio *s,int type)
{
    const char *problem=type!=3 && !selected(s)?"Select an object before importing assets.":
        !s->project.path?"Save the project before importing assets.":NULL;
    if(problem) {
        studio_status(s,problem);
        GtkAlertDialog *alert=gtk_alert_dialog_new("%s",problem);
        gtk_alert_dialog_set_detail(alert,type!=3 && !selected(s)?
            "Choose an object in the list, then click Import again.":
            "Use Save to choose a project location, then click Import again. Imported files are copied into the project's data folder.");
        gtk_alert_dialog_show(alert,GTK_WINDOW(s->window));g_object_unref(alert);return;
    }
    GtkFileDialog *dialog=gtk_file_dialog_new();gtk_file_dialog_set_modal(dialog,TRUE);gtk_file_dialog_set_title(dialog,type==0?"Import OBJ":type==1?"Import PNG texture":type==2?"Import C89 module":"Import WAV audio");
    GtkFileFilter *filter=gtk_file_filter_new();gtk_file_filter_add_pattern(filter,type==0?"*.obj":type==1?"*.png":type==2?"*.c":"*.wav");
    gtk_file_filter_add_pattern(filter,type==0?"*.OBJ":type==1?"*.PNG":type==2?"*.C":"*.WAV");
    GListStore *filters=g_list_store_new(GTK_TYPE_FILE_FILTER);g_list_store_append(filters,filter);
    gtk_file_dialog_set_filters(dialog,G_LIST_MODEL(filters));g_object_unref(filters);g_object_unref(filter);
    if(type==3) {
        g_object_set_data_full(G_OBJECT(dialog),"audio-directory",project_data_path(s->project.path),g_free);
        char *directory=g_path_get_dirname(s->project.path);GFile *folder=g_file_new_for_path(directory);
        gtk_file_dialog_set_initial_folder(dialog,folder);g_object_unref(folder);g_free(directory);
    }
    g_object_set_data(G_OBJECT(dialog),"import-type",GINT_TO_POINTER(type));gtk_file_dialog_open(dialog,GTK_WINDOW(s->window),NULL,imported,s);g_object_unref(dialog);
}
static void import_clicked(GtkButton *button,Studio *s)
{
    import_asset(s,GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"import-type")));
}
static void detach_clicked(GtkButton *button,Studio *s)
{
    Object *o=selected(s);if(!o) return;studio_checkpoint(s,NULL);viewport_clear_assets(s);
    if(g_object_get_data(G_OBJECT(button),"detach-module")) {
        g_free(o->module);o->module=g_strdup("");
    } else {
        g_free(o->asset);g_free(o->texture);o->asset=g_strdup("");o->texture=g_strdup("");
    }
    s->project.dirty=TRUE;studio_refresh(s);
}
static gboolean tools_clicked(GtkButton *button,Studio *s)
{
    (void)button;
    const char *dosbox=gtk_editable_get_text(GTK_EDITABLE(s->dosbox));
    char *program=*dosbox?g_find_program_in_path(dosbox):g_find_program_in_path("dosbox-x");
    if(!program && !*dosbox) program=g_find_program_in_path("dosbox");
    if(!program && !*dosbox) program=g_find_program_in_path("flatpak");
    if(!program) {studio_status(s,"DOSBox not found: specify its executable path.");return FALSE;}
    g_free(program);
    const char *bundled[]={"pipeline/cdrom/BUILDCD.EXE","pipeline/cdrom/STRIPISO.EXE"};
    for(guint k=0;k<G_N_ELEMENTS(bundled);k++) {
        char *path=g_build_filename(s->root,bundled[k],NULL);
        gboolean exists=g_file_test(path,G_FILE_TEST_IS_REGULAR);g_free(path);
        if(!exists) {studio_status(s,"A bundled tool is missing: check pipeline/cdrom in the PSX Studio folder.");return FALSE;}
    }
    g_free(s->project.dosbox);s->project.dosbox=g_strdup(dosbox);
    s->project.dirty=TRUE;title(s);studio_status(s,"DOSBox verified and applied. Native PNG/TIM conversion; BUILDCD and STRIPISO are bundled.");
    return TRUE;
}

static void object_camera_changed(GtkWidget *widget,Studio *s)
{
    if(s->refreshing) return;
    Object *o=selected(s);if(!o || o->kind!=OBJECT_CAMERA) return;
    studio_checkpoint(s,widget);
    if(widget==s->camera_active) s->project.active_camera=gtk_check_button_get_active(GTK_CHECK_BUTTON(widget))?o->id:0;
    else o->camera_zoom=gtk_spin_button_get_value(GTK_SPIN_BUTTON(s->camera_zoom));
    s->project.dirty=TRUE;studio_refresh(s);
}
static void camera_projection_changed(GObject *widget,GParamSpec *property,Studio *s)
{
    (void)widget;(void)property;
    if(s->refreshing || !selected(s) || selected(s)->kind!=OBJECT_CAMERA) return;
    studio_checkpoint(s,widget);
    selected(s)->camera_projection=gtk_drop_down_get_selected(GTK_DROP_DOWN(s->camera_projection));
    s->project.dirty=TRUE;title(s);
}
static void capture_camera(GtkButton *button,Studio *s)
{
    (void)button;Object *o=selected(s);if(!o || o->kind!=OBJECT_CAMERA) return;
    studio_checkpoint(s,NULL);project_camera_from_view(&s->project,o);s->project.dirty=TRUE;studio_refresh(s);
}
static void view_camera(GtkButton *button,Studio *s)
{
    (void)button;Object *o=selected(s);if(!o || o->kind!=OBJECT_CAMERA) return;
    studio_checkpoint(s,NULL);
    double yaw=o->rotation[1]*G_PI/180,pitch=o->rotation[0]*G_PI/180;
    double direction[]={sin(yaw)*cos(pitch),sin(pitch),cos(yaw)*cos(pitch)};
    double position[3];project_camera_position(&s->project,o,position);
    for(int k=0;k<3;k++) s->project.camera[k]=position[k]-600*direction[k]-(k==2?600:0);
    s->project.view[0]=remainder(o->rotation[1],360);s->project.view[1]=o->rotation[0];
    s->project.view[2]=o->camera_zoom;s->project.projection=o->camera_projection;
    s->project.dirty=TRUE;studio_refresh(s);
    studio_status(s,"Viewing the selected camera. Orbiting changes only the editor view; use Capture editor view to update the camera.");
}
static void frame_changed(GtkCheckButton *button,Studio *s)
{
    if(s->refreshing) return;
    studio_checkpoint(s,NULL);
    s->project.show_game_frame=gtk_check_button_get_active(button);s->project.dirty=TRUE;
    title(s);gtk_gl_area_queue_render(GTK_GL_AREA(s->viewport));
}
static void preset_changed(GObject *widget,GParamSpec *property,Studio *s)
{
    (void)property;if(s->refreshing) return;
    guint n=gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
    const double yaw[]={31.5,0,0,0,180,-90,90},pitch[]={34.4,90,-90,0,0,0,0};
    if(n>6) return;
    studio_checkpoint(s,NULL);
    s->project.view[0]=yaw[n];s->project.view[1]=pitch[n];
    if(n) s->project.projection=0;
    s->project.drag_axis=n==5 || n==6?0:n==3 || n==4?2:1;
    s->dragging=FALSE;s->project.dirty=TRUE;studio_refresh(s);
}
static void plane_changed(GObject *widget,GParamSpec *property,Studio *s)
{
    (void)property;if(s->refreshing) return;
    studio_checkpoint(s,NULL);
    s->project.drag_axis=gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
    s->dragging=FALSE;s->project.dirty=TRUE;studio_refresh(s);
}

static void scene_selected(GObject *widget,GParamSpec *property,Studio *s)
{
    (void)property;if(s->refreshing) return;
    guint index=gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));if(index>=s->project.scenes->len) return;
    guint id=((Scene*)g_ptr_array_index(s->project.scenes,index))->id;if(id==s->project.scene_id) return;
    studio_checkpoint(s,NULL);viewport_clear_assets(s);GError *error=NULL;
    if(!project_scene_switch(&s->project,id,&error)) {studio_status(s,error->message);g_error_free(error);}
    s->selected=-1;s->project.dirty=TRUE;studio_refresh(s);
}
static void scene_add_clicked(GtkButton *button,Studio *s)
{(void)button;studio_checkpoint(s,NULL);viewport_clear_assets(s);if(!project_scene_add(&s->project,NULL)) studio_status(s,"Maximum 16 Scenes per project.");s->selected=-1;studio_refresh(s);}
static void scene_delete_clicked(GtkButton *button,Studio *s)
{
    (void)button;studio_checkpoint(s,NULL);viewport_clear_assets(s);GError *error=NULL;
    if(!project_scene_delete(&s->project,s->project.scene_id,&error)) {studio_status(s,error->message);g_error_free(error);}
    s->selected=-1;studio_refresh(s);
}
static void scene_renamed(GtkEditable *entry,Studio *s)
{
    if(s->refreshing) return;
    studio_checkpoint(s,entry);
    Scene *scene=project_scene_find(&s->project,s->project.scene_id);g_free(scene->name);scene->name=g_strdup(gtk_editable_get_text(entry));
    s->project.dirty=TRUE;title(s);
}
static void scene_start_toggled(GtkCheckButton *button,Studio *s)
{if(s->refreshing) return;if(!gtk_check_button_get_active(button)) {studio_refresh(s);return;}studio_checkpoint(s,NULL);s->project.startup_scene=s->project.scene_id;s->project.dirty=TRUE;studio_refresh(s);}
static void extra_selected(GObject *widget,GParamSpec *property,Studio *s)
{
    (void)property;if(s->refreshing || !selected(s)) return;
    guint index=gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));Object *o=selected(s);studio_checkpoint(s,widget);
    if(widget==G_OBJECT(s->collision_scene)) {
        o->target_scene=index>0 && index<=s->project.scenes->len?((Scene*)g_ptr_array_index(s->project.scenes,index-1))->id:0;
        if(!o->target_scene && o->collision_action==3) o->collision_action=0;
    } else {
        Object *target=index>0 && index<=s->project.objects->len?g_ptr_array_index(s->project.objects,index-1):NULL;
        if(target && (target==o || target->kind==OBJECT_CAMERA)) {studio_status(s,"Choose a visual object to follow.");studio_refresh(s);return;}
        double position[3];project_camera_position(&s->project,o,position);memcpy(o->position,position,sizeof(position));
        o->follow_target=target?target->id:0;
        if(target) for(int k=0;k<3;k++) o->follow_offset[k]=position[k]-target->position[k];
    }
    s->project.dirty=TRUE;studio_refresh(s);
}
static void offset_changed(GtkSpinButton *spin,Studio *s)
{
    if(s->refreshing || !selected(s)) return;
    studio_checkpoint(s,spin);
    for(int k=0;k<3;k++) {
        if(GTK_WIDGET(spin)==s->collider_offset[k]) selected(s)->collider_offset[k]=gtk_spin_button_get_value(spin);
        if(GTK_WIDGET(spin)==s->follow_offset[k]) selected(s)->follow_offset[k]=gtk_spin_button_get_value(spin);
    }
    s->project.dirty=TRUE;title(s);gtk_gl_area_queue_render(GTK_GL_AREA(s->viewport));
}
static void colliders_toggled(GtkCheckButton *button,Studio *s)
{if(s->refreshing) return;studio_checkpoint(s,NULL);s->project.show_colliders=gtk_check_button_get_active(button);s->project.dirty=TRUE;title(s);gtk_gl_area_queue_render(GTK_GL_AREA(s->viewport));}

static void dropdown_setup(GtkSignalListItemFactory *factory,GObject *item,gpointer data)
{
    (void)factory;(void)data;GtkWidget *label=gtk_label_new("");gtk_label_set_xalign(GTK_LABEL(label),0);
    gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);gtk_label_set_max_width_chars(GTK_LABEL(label),20);
    gtk_list_item_set_child(GTK_LIST_ITEM(item),label);
}
static void dropdown_bind(GtkSignalListItemFactory *factory,GObject *item,gpointer data)
{
    (void)factory;(void)data;GtkWidget *label=gtk_list_item_get_child(GTK_LIST_ITEM(item));
    const char *text=gtk_string_object_get_string(GTK_STRING_OBJECT(gtk_list_item_get_item(GTK_LIST_ITEM(item))));
    gtk_label_set_text(GTK_LABEL(label),text);gtk_widget_set_tooltip_text(label,text);
}
static void compact_dropdown(GtkWidget *widget)
{
    GtkListItemFactory *factory=gtk_signal_list_item_factory_new();
    g_signal_connect(factory,"setup",G_CALLBACK(dropdown_setup),NULL);g_signal_connect(factory,"bind",G_CALLBACK(dropdown_bind),NULL);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(widget),factory);g_object_unref(factory);
}

static void inspector_tab_changed(GtkToggleButton *button, GtkStack *stack)
{
    if(gtk_toggle_button_get_active(button))
        gtk_stack_set_visible_child_name(stack,g_object_get_data(G_OBJECT(button),"inspector-page"));
}

static void inspector(Studio *s, GtkWidget *container)
{
    const char *tabs[]={"Object","Collision","Assets","Logic"};
    GtkWidget *pages[G_N_ELEMENTS(tabs)];
    GtkWidget *header=gtk_grid_new(), *stack=gtk_stack_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(header),TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(header),2);
    gtk_grid_set_row_spacing(GTK_GRID(header),2);
    gtk_box_append(GTK_BOX(container),header);
    gtk_box_append(GTK_BOX(container),stack);
    gtk_widget_set_vexpand(stack,TRUE);
    gtk_stack_set_hhomogeneous(GTK_STACK(stack),FALSE);
    gtk_stack_set_vhomogeneous(GTK_STACK(stack),FALSE);
    GtkToggleButton *first=NULL;
    for(guint i=0;i<G_N_ELEMENTS(tabs);i++) {
        GtkWidget *tab=gtk_toggle_button_new_with_label(tabs[i]);
        gtk_widget_add_css_class(tab,"inspector-tab");
        gtk_widget_set_hexpand(tab,TRUE);
        if(first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(tab),first);
        else first=GTK_TOGGLE_BUTTON(tab);
        gtk_grid_attach(GTK_GRID(header),tab,i,0,1,1);
        pages[i]=panel();
        GtkWidget *scroll=gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),pages[i]);
        gtk_stack_add_named(GTK_STACK(stack),scroll,tabs[i]);
        g_object_set_data(G_OBJECT(tab),"inspector-page",(gpointer)tabs[i]);
        g_signal_connect(tab,"toggled",G_CALLBACK(inspector_tab_changed),stack);
    }
    gtk_toggle_button_set_active(first,TRUE);
    GtkWidget *box=pages[0];
    gtk_box_append(GTK_BOX(box), heading("OBJECT PROPERTIES"));
    s->name = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(s->name), "Object name");
    gtk_box_append(GTK_BOX(box), s->name); g_signal_connect(s->name, "changed", G_CALLBACK(property_changed), s);
    const char *types[] = { "3D Object", "2D Sprite", "Sprite3D / Billboard", "Camera", NULL };
    s->kind = gtk_drop_down_new_from_strings(types); gtk_box_append(GTK_BOX(box), s->kind);
    g_signal_connect(s->kind, "notify::selected", G_CALLBACK(kind_changed), s);
    s->camera_panel=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
    gtk_box_append(GTK_BOX(s->camera_panel),heading("GAME CAMERA"));
    s->camera_active=gtk_check_button_new_with_label("Active game camera");gtk_box_append(GTK_BOX(s->camera_panel),s->camera_active);
    g_signal_connect(s->camera_active,"toggled",G_CALLBACK(object_camera_changed),s);
    const char *projections[]={"Orthographic","Perspective",NULL};
    gtk_box_append(GTK_BOX(s->camera_panel),gtk_label_new("Projection"));
    s->camera_projection=gtk_drop_down_new_from_strings(projections);gtk_box_append(GTK_BOX(s->camera_panel),s->camera_projection);
    g_signal_connect(s->camera_projection,"notify::selected",G_CALLBACK(camera_projection_changed),s);
    gtk_box_append(GTK_BOX(s->camera_panel),gtk_label_new("Camera zoom"));
    s->camera_zoom=gtk_spin_button_new_with_range(.15,6,.05);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s->camera_zoom),2);
    gtk_box_append(GTK_BOX(s->camera_panel),s->camera_zoom);g_signal_connect(s->camera_zoom,"value-changed",G_CALLBACK(object_camera_changed),s);
    gtk_box_append(GTK_BOX(s->camera_panel),gtk_label_new("Follow object"));
    s->follow_target=gtk_drop_down_new_from_strings((const char*[]){"No follow target",NULL});compact_dropdown(s->follow_target);gtk_box_append(GTK_BOX(s->camera_panel),s->follow_target);
    g_signal_connect(s->follow_target,"notify::selected",G_CALLBACK(extra_selected),s);
    gtk_box_append(GTK_BOX(s->camera_panel),gtk_label_new("Follow offset X / Y / Z"));
    GtkWidget *follow=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_append(GTK_BOX(s->camera_panel),follow);
    for(int k=0;k<3;k++) {s->follow_offset[k]=gtk_spin_button_new_with_range(-32767,32767,1);gtk_widget_set_hexpand(s->follow_offset[k],TRUE);gtk_editable_set_width_chars(GTK_EDITABLE(s->follow_offset[k]),5);gtk_box_append(GTK_BOX(follow),s->follow_offset[k]);g_signal_connect(s->follow_offset[k],"value-changed",G_CALLBACK(offset_changed),s);}
    button(s->camera_panel,"View through camera",G_CALLBACK(view_camera),s);
    button(s->camera_panel,"Capture editor view",G_CALLBACK(capture_camera),s);
    gtk_widget_set_tooltip_text(s->camera_panel,"Position is the camera eye. Rotation X sets pitch and Y sets yaw; roll is unsupported. Follow offset Y sets height above the target.");
    const char *groups[] = { "Position", "Rotation (degrees)", "Scale", "Collision: half extents" };
    const char *axes[] = { "X", "Y", "Z" };
    for (int group = 0; group < 4; group++) {
        if(group==3) {box=pages[1];gtk_box_append(GTK_BOX(box),heading("COLLISION"));}
        GtkWidget *section=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_box_append(GTK_BOX(box),section);
        if(group==2) s->scale_panel=section;
        gtk_box_append(GTK_BOX(section), gtk_label_new(groups[group]));
        GtkWidget *grid = gtk_grid_new(); gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
        gtk_box_append(GTK_BOX(section), grid);
        for (int axis = 0; axis < 3; axis++) {
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new(axes[axis]), axis, 0, 1, 1);
            double min = group >= 2 ? .01 : group == 1 ? -36000 : -32767;
            double max = group == 2 ? 128 : group == 1 ? 36000 : 32767;
            GtkWidget *spin = gtk_spin_button_new_with_range(min, max, group == 2 ? .1 : 1);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), group == 2 ? 2 : 1);
            gtk_editable_set_width_chars(GTK_EDITABLE(spin), 5);
            gtk_widget_set_hexpand(spin, TRUE);
            s->numbers[group][axis] = spin; gtk_grid_attach(GTK_GRID(grid), spin, axis, 1, 1, 1);
            g_signal_connect(spin, "value-changed", G_CALLBACK(property_changed), s);
        }
    }
    gtk_box_append(GTK_BOX(pages[0]),s->camera_panel);
    gtk_box_append(GTK_BOX(box),gtk_label_new("Collider center offset X / Y / Z"));
    GtkWidget *offset=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_append(GTK_BOX(box),offset);
    for(int k=0;k<3;k++) {s->collider_offset[k]=gtk_spin_button_new_with_range(-32767,32767,1);gtk_widget_set_hexpand(s->collider_offset[k],TRUE);gtk_editable_set_width_chars(GTK_EDITABLE(s->collider_offset[k]),5);gtk_box_append(GTK_BOX(offset),s->collider_offset[k]);g_signal_connect(s->collider_offset[k],"value-changed",G_CALLBACK(offset_changed),s);}
    s->solid = gtk_check_button_new_with_label("Solid (AABB volume)"); gtk_box_append(GTK_BOX(box), s->solid);
    g_signal_connect(s->solid, "toggled", G_CALLBACK(property_changed), s);
    box=pages[2];
    gtk_box_append(GTK_BOX(box),heading("MODEL AND TEXTURE"));
    gtk_widget_set_tooltip_text(box,"PNG up to 256x256; maximum 6 textures per Scene. Each object can use one model and texture.");
    const char *imports[]={"Import OBJ model","Import PNG texture","Import C89 module"};
    GtkWidget **labels[]={&s->asset_label,&s->texture_label,&s->module_label};
    for(int k=0;k<3;k++) {
        box=k==2?pages[3]:pages[2];
        if(k==2) gtk_box_append(GTK_BOX(box),heading("C89 MODULE"));
        *labels[k]=gtk_label_new("");gtk_label_set_ellipsize(GTK_LABEL(*labels[k]),PANGO_ELLIPSIZE_MIDDLE);gtk_box_append(GTK_BOX(box),*labels[k]);
        GtkWidget *b=button(box,imports[k],G_CALLBACK(import_clicked),s);if(k<2) s->visual_imports[k]=b;g_object_set_data(G_OBJECT(b),"import-type",GINT_TO_POINTER(k));
    }
    button(pages[2],"Remove model and texture",G_CALLBACK(detach_clicked),s);
    GtkWidget *remove_module=button(pages[3],"Remove module",G_CALLBACK(detach_clicked),s);
    g_object_set_data(G_OBJECT(remove_module),"detach-module",GINT_TO_POINTER(1));
    box=pages[3];
    gtk_box_append(GTK_BOX(box),heading("BEHAVIORS"));
    const char *inputs[]={"No movement","D-pad: move X/Z","D-pad: move X/Y",NULL};
    const char *collisions[]={"Collision: no action","Collision: kill this object","Collision: kill the other object","Collision: Change Scene",NULL};
    s->input_mode=gtk_drop_down_new_from_strings(inputs);gtk_box_append(GTK_BOX(box),s->input_mode);g_signal_connect(s->input_mode,"notify::selected",G_CALLBACK(behavior_selected),s);
    gtk_box_append(GTK_BOX(box),gtk_label_new("Speed (units per step)"));
    s->speed=gtk_spin_button_new_with_range(1,32,1);gtk_box_append(GTK_BOX(box),s->speed);g_signal_connect(s->speed,"value-changed",G_CALLBACK(behavior_changed),s);
    gtk_widget_set_tooltip_text(s->speed,"50 simulation steps per second: speed 2 = 100 world units per second.");
    s->collision_action=gtk_drop_down_new_from_strings(collisions);gtk_box_append(GTK_BOX(box),s->collision_action);g_signal_connect(s->collision_action,"notify::selected",G_CALLBACK(behavior_selected),s);
    gtk_box_append(GTK_BOX(box),gtk_label_new("Change Scene destination"));
    s->collision_scene=gtk_drop_down_new_from_strings((const char*[]){"No destination",NULL});compact_dropdown(s->collision_scene);gtk_box_append(GTK_BOX(box),s->collision_scene);
    g_signal_connect(s->collision_scene,"notify::selected",G_CALLBACK(extra_selected),s);

}

static void refresh_output(Studio *s)
{
    if(!g_action_map_lookup_action(G_ACTION_MAP(s->window),"run-emulator")) return;
    g_free(s->output_directory);s->output_directory=project_build_output(&s->project,s->root);
    gboolean available=s->output_directory && !s->build_process;
    menu_enabled(s,"open-folder",available);
    menu_enabled(s,"open-cue",available);
    menu_enabled(s,"run-emulator",available && !s->emulator_process && *s->project.emulator_command);
}

static void output_launched(GObject *source,GAsyncResult *result,gpointer data)
{
    Studio *s=data;GError *error=NULL;
    gboolean folder=GPOINTER_TO_INT(g_object_get_data(source,"open-folder"));
    gboolean ok=folder?gtk_file_launcher_open_containing_folder_finish(GTK_FILE_LAUNCHER(source),result,&error):
        gtk_file_launcher_launch_finish(GTK_FILE_LAUNCHER(source),result,&error);
    if(!ok && !g_error_matches(error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_DISMISSED)) {
        studio_status(s,error?error->message:"Cannot open the output.");
    }
    g_clear_error(&error);
}

static void open_output(gboolean folder,Studio *s)
{
    refresh_output(s);
    if(!s->output_directory || s->build_process) return;
    char *cue=g_build_filename(s->output_directory,"cdrom","game.cue",NULL);
    GFile *file=g_file_new_for_path(cue);g_free(cue);
    GtkFileLauncher *launcher=gtk_file_launcher_new(file);g_object_unref(file);
    g_object_set_data(G_OBJECT(launcher),"open-folder",GINT_TO_POINTER(folder));
    if(folder) gtk_file_launcher_open_containing_folder(launcher,GTK_WINDOW(s->window),NULL,output_launched,s);
    else {
        gtk_file_launcher_set_always_ask(launcher,TRUE);
        gtk_file_launcher_launch(launcher,GTK_WINDOW(s->window),NULL,output_launched,s);
    }
    g_object_unref(launcher);
}

static void emulator_finished(GObject *source,GAsyncResult *result,gpointer data)
{
    GWeakRef *reference=data;GtkWindow *window=g_weak_ref_get(reference);GError *error=NULL;
    gboolean ok=g_subprocess_wait_check_finish(G_SUBPROCESS(source),result,&error);
    if(window) {
        g_clear_object(&studio.emulator_process);
        studio_status(&studio,ok?"Emulator closed.":error->message);refresh_output(&studio);
        g_object_unref(window);
    }
    g_clear_error(&error);g_weak_ref_clear(reference);g_free(reference);
}
static void launch_emulator(GtkButton *button,Studio *s)
{
    (void)button;refresh_output(s);
    if(!s->output_directory || s->build_process || s->emulator_process) return;
    char *cue=g_build_filename(s->output_directory,"cdrom","game.cue",NULL);
    char *iso=g_build_filename(s->output_directory,"cdrom","GAME.ISO",NULL);
    GError *error=NULL;char **argv=project_emulator_argv(s->project.emulator_command,cue,iso,&error);
    g_free(cue);g_free(iso);
    if(argv) {
        GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
        char *directory=g_build_filename(s->output_directory,"cdrom",NULL);
        g_subprocess_launcher_set_cwd(launcher,directory);g_free(directory);
        s->emulator_process=g_subprocess_launcher_spawnv(launcher,(const char *const*)argv,&error);
        g_object_unref(launcher);g_strfreev(argv);
    }
    if(error) {studio_status(s,error->message);g_error_free(error);return;}
    GWeakRef *reference=g_new0(GWeakRef,1);g_weak_ref_init(reference,s->window);
    g_subprocess_wait_check_async(s->emulator_process,NULL,emulator_finished,reference);
    studio_status(s,"Emulator started with the last successful build. Rebuild first to include project changes.");
    refresh_output(s);
}

static void apply_settings(GtkButton *button,Studio *s)
{
    (void)button;
    const char *command=gtk_editable_get_text(GTK_EDITABLE(s->emulator_entry));
    if(*command) {
        GError *error=NULL;char **argv=project_emulator_argv(command,"game.cue","GAME.ISO",&error);
        if(!argv) {studio_status(s,error->message);gtk_label_set_text(GTK_LABEL(s->settings_status),error->message);g_error_free(error);return;}
        g_strfreev(argv);
    }
    studio_checkpoint(s,NULL);
    if(*gtk_editable_get_text(GTK_EDITABLE(s->psyq)) && !configure(NULL,s)) {return;}
    if(!tools_clicked(NULL,s)) {return;}
    if(!*gtk_editable_get_text(GTK_EDITABLE(s->psyq))) {g_free(s->project.psyq_path);s->project.psyq_path=g_strdup("");}
    g_free(s->project.emulator_command);s->project.emulator_command=g_strdup(command);
    g_free(s->project.output_path);s->project.output_path=g_strdup("");s->project.dirty=TRUE;title(s);
    if(s->project.path) {
        GError *error=NULL;
        if(!project_save(&s->project,s->project.path,&error)) {studio_status(s,error->message);g_error_free(error);return;}
        title(s);
    }
    studio_status(s,s->project.path?"Settings saved in the project.":"Settings applied. Save the project to keep them.");
    gtk_widget_set_visible(s->settings_window,FALSE);
}

static void show_settings(GtkButton *button,Studio *s)
{
    (void)button;
    studio_refresh(s);gtk_label_set_text(GTK_LABEL(s->settings_status),"");
    char *previous=project_build_sdk(&s->project,s->root),*message;
    if(*s->project.psyq_path) {
        message=g_strdup("");
    } else if(previous) {
        gtk_editable_set_text(GTK_EDITABLE(s->psyq),previous);
        message=g_strdup("SDK recovered from the last successful build. Apply and save to keep it.");
    } else message=g_strdup("No SDK configured. Enter the Psy-Q folder containing psyq/, include/ and lib/.");
    gtk_label_set_text(GTK_LABEL(s->sdk_source),message);
    gtk_widget_set_visible(s->sdk_source,*message!=0);
    gtk_widget_set_tooltip_text(s->psyq,gtk_editable_get_text(GTK_EDITABLE(s->psyq)));
    gtk_editable_set_position(GTK_EDITABLE(s->psyq),0);
    g_free(previous);g_free(message);
    gtk_window_present(GTK_WINDOW(s->settings_window));
}

/* A folder picker for the SDK, an executable picker for DOSBox. */
static void settings_browsed(GObject *source,GAsyncResult *result,gpointer data)
{
    Studio *s=data;GError *error=NULL;
    gboolean sdk=GPOINTER_TO_INT(g_object_get_data(source,"sdk-picker"));
    GFile *file=sdk?gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source),result,&error):
                    gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source),result,&error);
    if(file) {
        char *path=g_file_get_path(file);
        if(path) {
            GtkWidget *entry=sdk?s->psyq:s->dosbox;
            gtk_editable_set_text(GTK_EDITABLE(entry),path);
            gtk_editable_set_position(GTK_EDITABLE(entry),0);
            gtk_widget_set_tooltip_text(entry,path);
        } else gtk_label_set_text(GTK_LABEL(s->settings_status),"Choose a local path.");
        g_free(path);g_object_unref(file);
    } else if(error && !g_error_matches(error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_DISMISSED))
        gtk_label_set_text(GTK_LABEL(s->settings_status),error->message);
    g_clear_error(&error);
}
static void browse_settings(GtkButton *button,Studio *s)
{
    gboolean sdk=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"sdk-picker"));
    GtkFileDialog *dialog=gtk_file_dialog_new();gtk_file_dialog_set_modal(dialog,TRUE);
    gtk_file_dialog_set_title(dialog,sdk?"Select Psy-Q SDK folder":"Select DOSBox executable");
    g_object_set_data(G_OBJECT(dialog),"sdk-picker",GINT_TO_POINTER(sdk));
    const char *path=gtk_editable_get_text(GTK_EDITABLE(sdk?s->psyq:s->dosbox));
    char *resolved=*path?(sdk?g_canonicalize_filename(path,NULL):g_find_program_in_path(path)):NULL;
    if(resolved) {
        GFile *file=g_file_new_for_path(resolved);
        if(sdk) gtk_file_dialog_set_initial_folder(dialog,file);
        else gtk_file_dialog_set_initial_file(dialog,file);
        g_object_unref(file);g_free(resolved);
    }
    if(sdk) gtk_file_dialog_select_folder(dialog,GTK_WINDOW(s->settings_window),NULL,settings_browsed,s);
    else gtk_file_dialog_open(dialog,GTK_WINDOW(s->settings_window),NULL,settings_browsed,s);
    g_object_unref(dialog);
}
static GtkWidget *settings_path(Studio *s,GtkWidget *box,const char *label,gboolean sdk)
{
    GtkWidget *title=gtk_label_new(label);gtk_label_set_xalign(GTK_LABEL(title),0);gtk_box_append(GTK_BOX(box),title);
    GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_append(GTK_BOX(box),row);
    GtkWidget *entry=gtk_entry_new();gtk_widget_set_hexpand(entry,TRUE);gtk_box_append(GTK_BOX(row),entry);
    GtkWidget *browse=button(row,"Browse...",G_CALLBACK(browse_settings),s);
    g_object_set_data(G_OBJECT(browse),"sdk-picker",GINT_TO_POINTER(sdk));
    return entry;
}
static void create_settings(Studio *s)
{
    s->settings_window=gtk_window_new();gtk_window_set_title(GTK_WINDOW(s->settings_window),"Project settings");
    gtk_window_set_transient_for(GTK_WINDOW(s->settings_window),GTK_WINDOW(s->window));
    gtk_window_set_modal(GTK_WINDOW(s->settings_window),TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(s->settings_window),TRUE);
    gtk_window_set_hide_on_close(GTK_WINDOW(s->settings_window),TRUE);
    gtk_window_set_default_size(GTK_WINDOW(s->settings_window),720,-1);
    create_titlebar(GTK_WINDOW(s->settings_window),"Project settings",FALSE);
    GtkWidget *box=panel();gtk_window_set_child(GTK_WINDOW(s->settings_window),box);
    gtk_box_append(GTK_BOX(box),heading("PROJECT"));
    GtkWidget *project_label=gtk_label_new("Project name");gtk_label_set_xalign(GTK_LABEL(project_label),0);gtk_box_append(GTK_BOX(box),project_label);
    s->project_name=gtk_entry_new();gtk_box_append(GTK_BOX(box),s->project_name);
    g_signal_connect(s->project_name,"changed",G_CALLBACK(project_name_changed),s);
    gtk_box_append(GTK_BOX(box),heading("BUILD TOOLS"));
    s->psyq=settings_path(s,box,"Psy-Q SDK folder",TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->psyq),"Select the folder containing psyq/, include/ and lib/");
    s->sdk_source=gtk_label_new("");gtk_label_set_wrap(GTK_LABEL(s->sdk_source),TRUE);
    gtk_label_set_selectable(GTK_LABEL(s->sdk_source),TRUE);gtk_label_set_xalign(GTK_LABEL(s->sdk_source),0);
    gtk_label_set_max_width_chars(GTK_LABEL(s->sdk_source),80);
    gtk_box_append(GTK_BOX(box),s->sdk_source);
    s->dosbox=settings_path(s,box,"DOSBox executable",FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->dosbox),"Automatic detection, or select an executable");
    gtk_box_append(GTK_BOX(box),heading("EMULATOR"));
    GtkWidget *label=gtk_label_new("Launch command");gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_append(GTK_BOX(box),label);
    s->emulator_entry=gtk_entry_new();gtk_box_append(GTK_BOX(box),s->emulator_entry);
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->emulator_entry),"pcsxr -cdfile {cue}");
    GtkWidget *note=gtk_label_new("Use {cue} or {iso} for the game image. Leave empty to disable.");
    gtk_label_set_wrap(GTK_LABEL(note),TRUE);gtk_label_set_xalign(GTK_LABEL(note),0);gtk_box_append(GTK_BOX(box),note);
    gtk_widget_set_tooltip_text(s->emulator_entry,"Quote paths with spaces. Arguments are passed directly, without a shell.");
    button(box,"Apply and save",G_CALLBACK(apply_settings),s);
    s->settings_status=gtk_label_new("");gtk_label_set_wrap(GTK_LABEL(s->settings_status),TRUE);
    gtk_box_append(GTK_BOX(box),s->settings_status);
}

static gboolean smoke_snapshot(gpointer data)
{
    Studio *s = data;
    GError *error = gtk_gl_area_get_error(GTK_GL_AREA(s->viewport));
    if (error) { g_printerr("%s\n", error->message); exit_status = 1; }
    else {
        GdkPaintable *paintable = gtk_widget_paintable_new(s->window);
        GtkSnapshot *snapshot = gtk_snapshot_new();
        gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), gtk_widget_get_width(s->window), gtk_widget_get_height(s->window));
        GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
        GdkTexture *texture = node ? gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(s->window)), node, NULL) : NULL;
        if (!texture || !gdk_texture_save_to_png(texture, s->smoke_path)) {
            g_printerr("Cannot save the preview.\n"); exit_status = 1;
        }
        if (texture) g_object_unref(texture);
        if (node) gsk_render_node_unref(node);
        g_object_unref(paintable);
    }
    gtk_window_destroy(GTK_WINDOW(s->window));
    return G_SOURCE_REMOVE;
}

/* Small pixel-style caption controls, independent of the desktop icon theme. */
static void caption_icon(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    GtkWindow *window = data;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "caption-action"));
    (void)width; (void)height;
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, 0, 0, 0);
    if (action == 0) {
        cairo_rectangle(cr, 2, 9, 7, 2); cairo_fill(cr);
    } else if (action == 1) {
        cairo_set_line_width(cr, 1);
        if (gtk_window_is_maximized(window)) {
            cairo_rectangle(cr, 4.5, 1.5, 6, 6); cairo_stroke(cr);
            cairo_rectangle(cr, 4, 1, 7, 2); cairo_fill(cr);
            cairo_set_source_rgb(cr, .753, .753, .753);
            cairo_rectangle(cr, 1, 4, 8, 7); cairo_fill(cr);
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, 1.5, 4.5, 7, 6); cairo_stroke(cr);
            cairo_rectangle(cr, 1, 4, 8, 2); cairo_fill(cr);
        } else {
            cairo_rectangle(cr, 1.5, 1.5, 9, 9); cairo_stroke(cr);
            cairo_rectangle(cr, 1, 1, 10, 2); cairo_fill(cr);
        }
    } else {
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, 2, 2); cairo_line_to(cr, 10, 10);
        cairo_move_to(cr, 10, 2); cairo_line_to(cr, 2, 10); cairo_stroke(cr);
    }
}

static void caption_clicked(GtkButton *button, GtkWindow *window)
{
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "caption-action"));
    if (action == 0) gtk_window_minimize(window);
    else if (action == 1) {
        if (gtk_window_is_maximized(window)) gtk_window_unmaximize(window);
        else gtk_window_maximize(window);
    } else gtk_window_close(window);
}

static void caption_maximized(GObject *window, GParamSpec *property, GtkWidget *button)
{
    (void)property;
    const char *label = gtk_window_is_maximized(GTK_WINDOW(window)) ? "Restore" : "Maximize";
    gtk_widget_set_tooltip_text(button, label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(button)));
}

static GtkWidget *create_titlebar(GtkWindow *window,const char *text,gboolean full_controls)
{
    GtkWidget *handle = gtk_window_handle_new();
    gtk_widget_add_css_class(handle, "titlebar95");
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), row);
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(row), label);
    if(full_controls) {
        GtkWidget *version=gtk_label_new("v" STUDIO_APP_VERSION);
        gtk_widget_set_tooltip_text(version,"PSX Studio " STUDIO_APP_VERSION);
        gtk_box_append(GTK_BOX(row),version);
    }
    GtkWidget *spacer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    gtk_widget_set_hexpand(spacer,TRUE);
    gtk_box_append(GTK_BOX(row),spacer);
    const char *labels[] = { "Minimize", "Maximize", "Close" };
    for (int action = full_controls?0:2; action < 3; action++) {
        GtkWidget *button = gtk_button_new(), *icon = gtk_drawing_area_new();
        gtk_widget_add_css_class(button, "caption95");
        gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(button, labels[action]);
        gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, labels[action], -1);
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(icon), 12);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(icon), 12);
        g_object_set_data(G_OBJECT(icon), "caption-action", GINT_TO_POINTER(action));
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), caption_icon, window, NULL);
        gtk_button_set_child(GTK_BUTTON(button), icon);
        g_object_set_data(G_OBJECT(button), "caption-action", GINT_TO_POINTER(action));
        g_signal_connect(button, "clicked", G_CALLBACK(caption_clicked), window);
        if (action == 1) g_signal_connect_object(window, "notify::maximized", G_CALLBACK(caption_maximized), button, 0);
        if (action == 2) gtk_widget_set_margin_start(button, 2);
        gtk_box_append(GTK_BOX(row), button);
    }
    gtk_window_set_titlebar(window, handle);
    return label;
}

static void menu_activate(GSimpleAction *action,GVariant *parameter,gpointer data)
{
    Studio *s=data;const char *name=g_action_get_name(G_ACTION(action));(void)parameter;
    if(g_str_equal(name,"show-log")) {
        gtk_widget_set_visible(s->log_panel,!gtk_widget_get_visible(s->log_panel));return;
    }
    if(s->build_process) return;
    if(g_str_equal(name,"new")) new_clicked(NULL,s);
    else if(g_str_equal(name,"open")) open_clicked(NULL,s);
    else if(g_str_equal(name,"save")) save_clicked(NULL,s);
    else if(g_str_equal(name,"save-as")) save_as_clicked(NULL,s);
    else if(g_str_equal(name,"import-audio")) import_asset(s,3);
    else if(g_str_equal(name,"settings")) show_settings(NULL,s);
    else if(g_str_equal(name,"exit")) request_action(s,2);
    else if(g_str_equal(name,"undo")) history_action(FALSE,s);
    else if(g_str_equal(name,"redo")) history_action(TRUE,s);
    else if(g_str_equal(name,"build")) build_clicked(NULL,s);
    else if(g_str_equal(name,"run-emulator")) launch_emulator(NULL,s);
    else if(g_str_equal(name,"open-folder")) open_output(TRUE,s);
    else if(g_str_equal(name,"open-cue")) open_output(FALSE,s);
}

static GtkWidget *create_menu(Studio *s)
{
    const char *groups[]={"_File","_Edit","_Build","_View","_Audio"};
    const char *names[]={"new","open","save","save-as","settings","exit","undo","redo","build","run-emulator","open-folder","open-cue","show-log","import-audio"};
    const char *labels[]={"_New","_Open…","_Save","Save _as…","Se_ttings…","E_xit","_Undo","_Redo","_Build","_Run emulator","Open _folder","Open _CUE with…","Show _log","Import _WAV audio…"};
    const int ends[]={6,8,12,13,14};GMenu *model=g_menu_new();int index=0;
    for(guint group=0;group<G_N_ELEMENTS(groups);group++) {
        GMenu *menu=g_menu_new();
        for(;index<ends[group];index++) {
            GSimpleAction *action=g_simple_action_new(names[index],NULL);
            g_signal_connect(action,"activate",G_CALLBACK(menu_activate),s);
            g_action_map_add_action(G_ACTION_MAP(s->window),G_ACTION(action));g_object_unref(action);
            char *detailed=g_strdup_printf("win.%s",names[index]);g_menu_append(menu,labels[index],detailed);g_free(detailed);
        }
        g_menu_append_submenu(model,groups[group],G_MENU_MODEL(menu));g_object_unref(menu);
    }
    GtkWidget *bar=gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(model));g_object_unref(model);
    gtk_widget_add_css_class(bar,"menu95");return bar;
}

static void activate(GtkApplication *app, Studio *s)
{
    s->window = gtk_application_window_new(app); gtk_window_set_default_size(GTK_WINDOW(s->window), 1280, 850);
    g_signal_connect(s->window, "close-request", G_CALLBACK(close_requested), s);
    s->title_label=create_titlebar(GTK_WINDOW(s->window),"PSX Studio",TRUE);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        "window { background: #c0c0c0; color: #111; font-family: Sans; font-size: 12px; }"
        "button { border-radius: 0; background: #c0c0c0; color: #111; border: 1px solid; border-color: #fff #000 #000 #fff; padding: 4px 8px; box-shadow: inset 1px 1px #dfdfdf, inset -1px -1px #808080; }"
        "button:active, button:checked { border-color: #000 #fff #fff #000; box-shadow: inset 1px 1px #808080, inset -1px -1px #dfdfdf; } button:focus-visible { outline: 1px dotted #000; outline-offset: -4px; } button:disabled { color: #808080; text-shadow: 1px 1px #fff; } entry, spinbutton, dropdown { border-radius: 0; background: #fff; color: #111; }"
        "menubar.menu95 { background: #c0c0c0; padding: 0; }"
        "menubar.menu95 > item { min-height: 18px; padding: 2px 8px; border-radius: 0; }"
        "menubar.menu95 > item:selected { background: #000080; color: #fff; }"
        ".menu95 popover > contents { background: #c0c0c0; color: #111; border-radius: 0; border: 2px solid; border-color: #fff #000 #000 #fff; padding: 3px; box-shadow: none; }"
        ".menu95 popover scrollbar.vertical, .menu95 popover scrollbar.vertical range, .menu95 popover scrollbar.vertical trough, .menu95 popover scrollbar.vertical slider { min-height: 0; }"
        ".menu95 modelbutton { min-height: 20px; padding: 2px 12px; border-radius: 0; }"
        ".menu95 modelbutton:hover, .menu95 modelbutton:selected { background: #000080; color: #fff; }"
        "spinbutton button { padding: 0; min-width: 12px; min-height: 18px; border-width: 1px; }"
        "spinbutton text { min-width: 0; padding: 2px; }"
        "list { background: #fff; color: #111; border: 2px inset #eee; } row:selected { background: #000080; color: white; }"
        ".view-toolbar spinbutton { min-height: 24px; } .view-toolbar spinbutton entry { padding: 1px 3px; min-width: 0; } .view-toolbar spinbutton button { padding: 1px 3px; min-width: 12px; min-height: 20px; }"
        "button.inspector-tab:checked { background: #000080; color: #fff; }"
        ".heading95 { background: #000080; color: white; padding: 5px; font-weight: bold; }"
        "textview text { background: #fff; color: #111; }"
        "windowhandle.titlebar95 { background: #000080; color: #fff; min-height: 18px; padding: 6px 7px; border-radius: 0; box-shadow: none; }"
        ".titlebar95 label { color: #fff; font-size: 12px; font-weight: bold; margin: 0 3px; padding: 0; }"
        ".titlebar95 button.caption95 { min-width: 16px; min-height: 16px; padding: 0; margin: 0; border-radius: 0; }");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(s->window), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4); gtk_window_set_child(GTK_WINDOW(s->window), root);
    s->menubar=create_menu(s);gtk_box_append(GTK_BOX(root),s->menubar);
    gtk_widget_set_margin_bottom(s->menubar,3);
    create_settings(s);
    GtkWidget *body = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_widget_set_vexpand(body, TRUE); gtk_box_append(GTK_BOX(root), body);
    s->body = body;
    GtkWidget *left = panel(); gtk_widget_set_size_request(left, 210, -1);
    gtk_box_append(GTK_BOX(left),heading("SCENES"));
    s->scene_selector=gtk_drop_down_new_from_strings((const char*[]){"Scene 1",NULL});compact_dropdown(s->scene_selector);gtk_box_append(GTK_BOX(left),s->scene_selector);
    g_signal_connect(s->scene_selector,"notify::selected",G_CALLBACK(scene_selected),s);
    s->scene_name=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(s->scene_name),"Scene name");gtk_box_append(GTK_BOX(left),s->scene_name);g_signal_connect(s->scene_name,"changed",G_CALLBACK(scene_renamed),s);
    GtkWidget *scene_buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_append(GTK_BOX(left),scene_buttons);
    button(scene_buttons,"+ Scene",G_CALLBACK(scene_add_clicked),s);button(scene_buttons,"Delete Scene",G_CALLBACK(scene_delete_clicked),s);
    s->startup_button=gtk_check_button_new_with_label("Startup Scene");gtk_box_append(GTK_BOX(left),s->startup_button);g_signal_connect(s->startup_button,"toggled",G_CALLBACK(scene_start_toggled),s);
    gtk_box_append(GTK_BOX(left), heading("OBJECTS"));
    const char *labels[] = { "+ 3D Object", "+ Sprite", "+ Sprite3D", "+ Camera" };
    for (int i = 0; i < 4; i++) {
        GtkWidget *b = button(left, labels[i], G_CALLBACK(add_object), s);
        g_object_set_data(G_OBJECT(b), "kind", GINT_TO_POINTER(i));
    }
    s->list = gtk_list_box_new();
    g_signal_connect(s->list, "row-selected", G_CALLBACK(row_selected), s);
    GtkWidget *scroll = gtk_scrolled_window_new(); gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s->list); gtk_box_append(GTK_BOX(left), scroll);
    button(left, "Duplicate object", G_CALLBACK(duplicate_object), s); button(left, "Delete object", G_CALLBACK(delete_object), s);
    gtk_paned_set_start_child(GTK_PANED(body), left); gtk_paned_set_resize_start_child(GTK_PANED(body), FALSE);
    GtkWidget *right = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_paned_set_end_child(GTK_PANED(body), right);
    GtkWidget *center = panel(), *view_header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_append(GTK_BOX(center),view_header);
    gtk_widget_add_css_class(view_header,"view-toolbar");
    gtk_widget_set_tooltip_text(view_header,"Editor view only. Select a Camera object to change the game camera.");
    gtk_box_append(GTK_BOX(view_header),gtk_label_new("View"));
    const char *camera_labels[]={"X","Y","Z","Yaw","Pitch","Zoom"};
    for(int k=0;k<6;k++) {
        GtkWidget *field=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
        gtk_box_append(GTK_BOX(view_header),field);
        gtk_box_append(GTK_BOX(field),gtk_label_new(camera_labels[k]));
        double min=k<3?-32000:k==3?-360:k==4?-90:.15,max=k<3?32000:k==3?360:k==4?90:6;
        s->camera_values[k]=gtk_spin_button_new_with_range(min,max,k==5?.05:1);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s->camera_values[k]),k==5?2:0);
        gtk_editable_set_width_chars(GTK_EDITABLE(s->camera_values[k]),k<3?4:3);
        gtk_editable_set_max_width_chars(GTK_EDITABLE(s->camera_values[k]),k<3?4:3);
        gtk_widget_set_hexpand(field,TRUE);gtk_widget_set_hexpand(s->camera_values[k],TRUE);
        gtk_box_append(GTK_BOX(field),s->camera_values[k]);
        g_signal_connect(s->camera_values[k],"value-changed",G_CALLBACK(camera_changed),s);
    }
    GtkWidget *view_tools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_append(GTK_BOX(center),view_tools);
    s->projection_button=button(view_tools,"View: Orthographic (P)",G_CALLBACK(toggle_projection),s);
    const char *presets[]={"Free orbit","Top","Bottom","Front","Back","Left","Right",NULL};
    s->view_preset=gtk_drop_down_new_from_strings(presets);gtk_box_append(GTK_BOX(view_tools),s->view_preset);
    gtk_widget_set_tooltip_text(s->view_preset,"Editor view direction; does not change Camera objects.");
    g_signal_connect(s->view_preset,"notify::selected",G_CALLBACK(preset_changed),s);
    const char *planes[]={"Drag: Y/Z","Drag: X/Z","Drag: X/Y",NULL};
    s->drag_plane=gtk_drop_down_new_from_strings(planes);gtk_box_append(GTK_BOX(view_tools),s->drag_plane);
    g_signal_connect(s->drag_plane,"notify::selected",G_CALLBACK(plane_changed),s);
    s->colliders_button=gtk_check_button_new_with_label("Show colliders");gtk_box_append(GTK_BOX(view_tools),s->colliders_button);g_signal_connect(s->colliders_button,"toggled",G_CALLBACK(colliders_toggled),s);
    s->frame_button=gtk_check_button_new_with_label("Show game frame");gtk_box_append(GTK_BOX(view_tools),s->frame_button);
    g_signal_connect(s->frame_button,"toggled",G_CALLBACK(frame_changed),s);
    s->viewport = viewport_create(s); gtk_box_append(GTK_BOX(center), s->viewport);
    GtkEventController *keys=gtk_event_controller_key_new();
    g_signal_connect(keys,"key-pressed",G_CALLBACK(projection_key),s);
    gtk_widget_add_controller(s->window,keys);
    s->drag_hint=gtk_label_new("");gtk_box_append(GTK_BOX(center),s->drag_hint);
    gtk_paned_set_start_child(GTK_PANED(right), center);
    GtkWidget *properties = panel(); inspector(s, properties);
    gtk_widget_set_size_request(properties,310,-1);
    gtk_paned_set_end_child(GTK_PANED(right),properties); gtk_paned_set_resize_end_child(GTK_PANED(right), FALSE);
    gtk_paned_set_position(GTK_PANED(body), 225); gtk_paned_set_position(GTK_PANED(right), 720);
    s->log = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(s->log), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s->log), GTK_WRAP_WORD_CHAR);
    GtkWidget *logs = gtk_scrolled_window_new(); gtk_widget_set_size_request(logs, -1, 72);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(logs), s->log); gtk_box_append(GTK_BOX(root), logs);
    s->log_panel=logs;gtk_widget_set_visible(logs,FALSE);
    s->status = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(s->status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(s->status), PANGO_ELLIPSIZE_END); gtk_box_append(GTK_BOX(root), s->status);
    studio_refresh(s); studio_status(s, "Editor ready. Create or open a .psx project.");
    gtk_window_present(GTK_WINDOW(s->window));
    if (s->build_and_exit) build_clicked(NULL, s);
    else if (s->smoke_path) g_timeout_add(1200, smoke_snapshot, s);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--build-ps1")) return studio_build_main(argc, argv);
    studio.undo=g_ptr_array_new_with_free_func((GDestroyNotify)json_node_free);studio.redo=g_ptr_array_new_with_free_func((GDestroyNotify)json_node_free);
    char *project_path = NULL;
    char *export_path = NULL, *output_path = NULL, *asset_root = NULL;
    char *wav_path=NULL,*audio_directory=NULL;
    gboolean demo = FALSE;
    GOptionEntry options[] = {
        {"import-wav",0,0,G_OPTION_ARG_FILENAME,&wav_path,"Import WAV as VAG without opening the UI","FILE"},
        {"audio-directory",0,0,G_OPTION_ARG_FILENAME,&audio_directory,"Destination data folder for --import-wav","DIRECTORY"},
        {"project", 'p', 0, G_OPTION_ARG_FILENAME, &project_path, "Open a .psx project", "FILE"},
        {"export", 0, 0, G_OPTION_ARG_FILENAME, &export_path, "Export the scene to C89 without opening the UI", "PROJECT"},
        {"asset-root",0,0,G_OPTION_ARG_FILENAME,&asset_root,"Data folder for snapshot export","DIRECTORY"},
        {"output", 0, 0, G_OPTION_ARG_FILENAME, &output_path, "Directory for scene.c and scene.h", "DIRECTORY"},
        {"build-and-exit", 0, 0, G_OPTION_ARG_NONE, &studio.build_and_exit, "Start a PS1 build from the UI and exit when finished", NULL},
        {"smoke-test", 0, 0, G_OPTION_ARG_FILENAME, &studio.smoke_path, "Save a preview and exit", "PNG"},
        {"demo", 0, 0, G_OPTION_ARG_NONE, &demo, "Show three demo objects", NULL},
        {NULL, 0, 0, 0, NULL, NULL, NULL}
    };
    GError *error = NULL;
    GOptionContext *options_context = g_option_context_new("- PSX Studio");
    g_option_context_add_main_entries(options_context, options, NULL);
    if (!g_option_context_parse(options_context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message); g_clear_error(&error); g_option_context_free(options_context); return 1;
    }
    g_option_context_free(options_context);
    if(wav_path || audio_directory) {
        if(!wav_path || !audio_directory || export_path || output_path || project_path || studio.build_and_exit || studio.smoke_path || demo || asset_root) {
            g_printerr("Use --import-wav FILE --audio-directory DIRECTORY on its own.\n");return 1;
        }
        char *path=audio_import_wav(wav_path,audio_directory,&error);
        if(path) g_print("WAV imported as VAG: %s\n",path);
        else {g_printerr("WAV import failed: %s\n",error->message);g_clear_error(&error);}
        int result=path?0:1;g_free(path);g_free(wav_path);g_free(audio_directory);
        g_ptr_array_unref(studio.undo);g_ptr_array_unref(studio.redo);return result;
    }
    if ((export_path && (!output_path || project_path || studio.build_and_exit)) || (!export_path && output_path) ||
        (studio.build_and_exit && !project_path)) {
        g_printerr("Use --export PROJECT --output DIRECTORY or --build-and-exit --project PROJECT.\n"); return 1;
    }
    if (export_path) project_path = g_strdup(export_path);
    project_init(&studio.project); studio.selected = -1; studio.zoom = .9; studio.yaw = .55; studio.pitch = .6;
    if (project_path && !project_load(&studio.project, project_path, &error)) {
        g_printerr("%s\n", error->message); g_clear_error(&error); project_clear(&studio.project); g_free(project_path); return 1;
    }
    if (project_path && studio.project.objects->len) studio.selected = 0;
    if (export_path) {
        studio.project.asset_root=asset_root?g_canonicalize_filename(asset_root,NULL):NULL;g_free(asset_root);
        gboolean ok = project_export(&studio.project, output_path, &error);
        if (ok) g_print("Exported %u Scene(s) to C89: %s\n", studio.project.scenes->len, output_path);
        else { g_printerr("Export failed: %s\n", error->message); g_clear_error(&error); }
        project_clear(&studio.project); g_free(project_path); g_free(export_path); g_free(output_path); return ok ? 0 : 1;
    }
    if (!project_path && demo) {
        for (int i = 0; i < 3; i++) {
            Object *o = project_add(&studio.project, i); o->position[0] = (i-1)*120; o->position[1] = 24;
            o->solid = i == 0;
        }
        studio.selected = 0;
    }
    studio.executable = g_file_read_link("/proc/self/exe", NULL);
    if (!studio.executable) studio.executable = g_find_program_in_path(argv[0]);
    if (!studio.executable) { g_printerr("Cannot locate the editor executable.\n"); return 1; }
    studio.root = studio_install_root(studio.executable);
#ifdef G_OS_WIN32
    /* Resource paths follow the extracted folder, independently of MSYS2. */
    const char *resource_names[]={"GSETTINGS_SCHEMA_DIR","GDK_PIXBUF_MODULE_FILE","FONTCONFIG_PATH","FONTCONFIG_FILE"};
    const char *resource_paths[]={"share/glib-2.0/schemas","lib/gdk-pixbuf-2.0/2.10.0/loaders.cache","etc/fonts","etc/fonts/fonts.conf"};
    for(guint i=0;i<G_N_ELEMENTS(resource_names);i++) {
        char *path=g_build_filename(studio.root,resource_paths[i],NULL);
        if(g_file_test(path,G_FILE_TEST_EXISTS)) g_setenv(resource_names[i],path,FALSE);
        g_free(path);
    }
    char *share=g_build_filename(studio.root,"share",NULL);
    if(g_file_test(share,G_FILE_TEST_IS_DIR)) {
        const char *previous=g_getenv("XDG_DATA_DIRS");
        char *data_dirs=previous?g_strconcat(share,G_SEARCHPATH_SEPARATOR_S,previous,NULL):g_strdup(share);
        g_setenv("XDG_DATA_DIRS",data_dirs,TRUE);g_free(data_dirs);
    }
    g_free(share);
#endif
    studio.application = gtk_application_new("org.psxstudio.GenericEditor", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(studio.application, "activate", G_CALLBACK(activate), &studio);
    int result = g_application_run(G_APPLICATION(studio.application), argc, argv);
    g_ptr_array_unref(studio.undo);g_ptr_array_unref(studio.redo);
    g_object_unref(studio.application); project_clear(&studio.project); g_free(project_path); g_free(studio.smoke_path);
    g_free(studio.executable); g_free(studio.root); g_free(studio.output_directory);
    return result ? result : exit_status;
}
