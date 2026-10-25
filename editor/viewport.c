#include "studio.h"
#include "viewport_shaders.h"
#include <math.h>

static PsxCamera camera(const Studio *s)
{
    PsxCamera c;
    double yaw=s->project.view[0]*G_PI/180,pitch=s->project.view[1]*G_PI/180;
    for(int k=0;k<3;k++) c.center[k]=s->project.camera[k]+(k==2?600:0);
    c.cy=cos(yaw);c.sy=sin(yaw);c.cp=cos(pitch);c.sp=sin(pitch);
    c.zoom=s->project.view[2];c.projection=s->project.projection;
    return c;
}

gboolean viewport_project(const Studio *s, const double point[3], double *x, double *y)
{
    PsxCamera c=camera(s);double screen[3];
    if(!psx_camera_project(&c,point,screen)) return FALSE;
    *x=gtk_widget_get_width(s->viewport)/2.0+screen[0];
    *y=gtk_widget_get_height(s->viewport)/2.0+screen[1];
    return TRUE;
}

static GLuint shader(GLenum type, const char *source, GError **error)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &source, NULL); glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "OpenGL: %s", log);
        glDeleteShader(sh); return 0;
    }
    return sh;
}

static void realize(GtkGLArea *area, Studio *s)
{
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area)) return;
    GError *error = NULL;
    gboolean es = !epoxy_is_desktop_gl();
    GLuint vertex = shader(GL_VERTEX_SHADER, es ? STUDIO_VERTEX_ES : STUDIO_VERTEX_GL, &error);
    GLuint fragment = error ? 0 : shader(GL_FRAGMENT_SHADER,
        es ? STUDIO_FRAGMENT_ES : STUDIO_FRAGMENT_GL, &error);
    if (!error) {
        s->program = glCreateProgram();
        glAttachShader(s->program, vertex); glAttachShader(s->program, fragment);
        glBindAttribLocation(s->program, 0, "point"); glBindAttribLocation(s->program,1,"uv"); glLinkProgram(s->program);
        GLint ok;
        glGetProgramiv(s->program, GL_LINK_STATUS, &ok);
        if (!ok) g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED, "OpenGL: shader linking failed.");
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (error) { gtk_gl_area_set_error(area, error); g_error_free(error); return; }
    gtk_gl_area_set_has_depth_buffer(area,TRUE);
    glGenVertexArrays(1, &s->vao); glGenBuffers(1, &s->vbo);
}

static void unrealize(GtkGLArea *area, Studio *s)
{
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area)) return;
    viewport_clear_assets(s);
    glDeleteBuffers(1, &s->vbo); glDeleteVertexArrays(1, &s->vao); glDeleteProgram(s->program);
    s->program = s->vao = s->vbo = 0;
}

void viewport_clear_assets(Studio *s)
{
    gboolean gl= s->viewport && gtk_widget_get_realized(s->viewport);
    if(gl) { gtk_gl_area_make_current(GTK_GL_AREA(s->viewport)); gl=!gtk_gl_area_get_error(GTK_GL_AREA(s->viewport)); }
    for(guint i=0;i<s->project.objects->len;i++) {
        Object *o=g_ptr_array_index(s->project.objects,i);
        if(gl && o->visual && o->visual->texture_id) glDeleteTextures(1,&o->visual->texture_id);
        asset_free(o->visual);o->visual=NULL;o->visual_attempted=FALSE;
    }
}

static void transform(Studio *s,const Object *o,const double *local,double *p)
{
    for(int k=0;k<3;k++) p[k]=local[k]*o->scale[k];
    if(o->kind==OBJECT_BILLBOARD) {
        double yaw=s->project.view[0]*G_PI/180,pitch=s->project.view[1]*G_PI/180,x=p[0],y=p[1];
        p[0]=cos(yaw)*x-sin(pitch)*sin(yaw)*y;
        p[1]=cos(pitch)*y;p[2]=-sin(yaw)*x-sin(pitch)*cos(yaw)*y;
    } else for(int k=0;k<3;k++) {
        int a=(k+1)%3,b=(k+2)%3;double angle=o->rotation[k]*G_PI/180,x=p[a],y=p[b];
        p[a]=cos(angle)*x-sin(angle)*y;p[b]=sin(angle)*x+cos(angle)*y;
    }
    for(int k=0;k<3;k++) p[k]+=o->position[k];
}

static void projected(Studio *s,const double *p,GLfloat *v)
{
    PsxCamera c=camera(s);double view[3];psx_camera_view(&c,p,view);
    double width=gtk_widget_get_width(s->viewport),height=gtk_widget_get_height(s->viewport);
    if(c.projection==PSX_PROJECTION_PERSPECTIVE) {
        v[0]=2*view[0]*c.zoom*PSX_CAMERA_DISTANCE/width;
        v[1]=-2*view[1]*c.zoom*PSX_CAMERA_DISTANCE/height;
        v[2]=((PSX_CAMERA_FAR+PSX_CAMERA_NEAR)*view[2]-2*PSX_CAMERA_FAR*PSX_CAMERA_NEAR)/(PSX_CAMERA_FAR-PSX_CAMERA_NEAR);
        v[3]=view[2];
    } else {
        v[0]=2*view[0]*c.zoom/width;v[1]=-2*view[1]*c.zoom/height;
        v[2]=(view[2]-PSX_CAMERA_DISTANCE)/131072;v[3]=1;
    }
}

static void draw_asset(Studio *s,Object *o)
{
    Asset *a=o->visual;
    if(!a) return;
    if(a->pixels && !a->texture_id) {
        glGenTextures(1,&a->texture_id);glBindTexture(GL_TEXTURE_2D,a->texture_id);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,a->width,a->height,0,GL_RGBA,GL_UNSIGNED_BYTE,a->pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,a->texture_id);
    glUniform1i(glGetUniformLocation(s->program,"textured"),a->texture_id?1:0);
    /* The selection outline is drawn separately; preserve texture colours. */
    glUniform4f(glGetUniformLocation(s->program,"tint"),a->texture_id?1:.5,
        a->texture_id?1:.72,1,1);
    GLfloat *vertices=g_new(GLfloat,a->vertices->len*6);
    for(guint i=0;i<a->vertices->len;i++) {
        AssetVertex *v=&g_array_index(a->vertices,AssetVertex,i);double p[3];transform(s,o,v->p,p);
        projected(s,p,&vertices[i*6]);
        vertices[i*6+4]=a->width?(asset_uv_texel(v->uv[0],a->width)+.5)/a->width:0;
        vertices[i*6+5]=a->height?(asset_uv_texel(v->uv[1],a->height)+.5)/a->height:0;
    }
    glEnable(GL_DEPTH_TEST);glBufferData(GL_ARRAY_BUFFER,a->vertices->len*6*sizeof(GLfloat),vertices,GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,a->vertices->len);glDisable(GL_DEPTH_TEST);g_free(vertices);
}

static void line(Studio *s, const double *a, const double *b, float r, float g, float blue)
{
    GLfloat vertices[12]={0};
    projected(s,a,vertices);projected(s,b,vertices+6);
    glUniform1i(glGetUniformLocation(s->program,"textured"),0);
    glUniform4f(glGetUniformLocation(s->program, "tint"), r, g, blue, 1);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, 2);
}

/* Screen-space triangles keep axis thickness consistent across GL drivers,
 * zoom levels and HiDPI displays, even where wide GL lines are unsupported. */
static void axis_stroke(Studio *s,const double *a,const double *b,
    float width,float r,float g,float blue)
{
    GLfloat ends[12]={0},vertices[36];
    projected(s,a,ends);projected(s,b,ends+6);
    float near_a=ends[2]+ends[3],near_b=ends[8]+ends[9];
    if(near_a<0 && near_b<0) return;
    if(near_a<0 || near_b<0) {
        float t=near_a/(near_a-near_b);
        int clipped=near_a<0?0:6;
        for(int k=0;k<4;k++) ends[clipped+k]=ends[k]+t*(ends[6+k]-ends[k]);
    }
    if(ends[3]<=0 || ends[9]<=0) return;
    double w=gtk_widget_get_width(s->viewport),h=gtk_widget_get_height(s->viewport);
    double dx=(ends[6]/ends[9]-ends[0]/ends[3])*w;
    double dy=(ends[7]/ends[9]-ends[1]/ends[3])*h;
    double length=hypot(dx,dy);
    if(length<.01) return;
    double ox=-dy/length*width/w,oy=dx/length*width/h;
    static const int end_index[]={0,0,1,0,1,1};
    static const int side[]={-1,1,1,-1,1,-1};
    for(int i=0;i<6;i++) {
        GLfloat *v=vertices+i*6,*e=ends+end_index[i]*6;
        for(int k=0;k<6;k++) v[k]=e[k];
        v[0]+=side[i]*ox*e[3];v[1]+=side[i]*oy*e[3];
    }
    glUniform1i(glGetUniformLocation(s->program,"textured"),0);
    glUniform4f(glGetUniformLocation(s->program,"tint"),r,g,blue,1);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES,0,6);
}

static void box(Studio *s, const Object *o, gboolean selected, gboolean collider)
{
    double vertices[8][3];
    static const int edges[12][2] = {
        {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}
    };
    double minimum[3]={-24,-24,-24}, maximum[3]={24,24,24};
    if (!collider && o->visual && o->visual->vertices->len) {
        const AssetVertex *first=&g_array_index(o->visual->vertices,AssetVertex,0);
        for(int k=0;k<3;k++) minimum[k]=maximum[k]=first->p[k];
        for(guint i=1;i<o->visual->vertices->len;i++) {
            const AssetVertex *vertex=&g_array_index(o->visual->vertices,AssetVertex,i);
            for(int k=0;k<3;k++) {
                minimum[k]=MIN(minimum[k],vertex->p[k]);
                maximum[k]=MAX(maximum[k],vertex->p[k]);
            }
        }
    }
    for (int v = 0; v < 8; v++) {
        double p[3],local[3];
        for(int k=0;k<3;k++) local[k]=collider?((v&(1<<k))?1:-1)*o->bounds[k]:
            ((v&(1<<k))?maximum[k]:minimum[k]);
        if(collider) for(int k=0;k<3;k++) p[k]=local[k]+o->position[k]+o->collider_offset[k];
        else {if(o->kind==OBJECT_SPRITE || o->kind==OBJECT_BILLBOARD) local[2]=0; transform(s,o,local,p);}
        for(int k=0;k<3;k++) vertices[v][k]=p[k];
    }
    float r = collider ? .2 : selected ? 1 : .5;
    float g = collider ? .85 : selected ? .85 : .72;
    float b = collider ? .4 : selected ? .2 : 1;
    for (int i = 0; i < 12; i++) line(s, vertices[edges[i][0]], vertices[edges[i][1]], r, g, b);
}

static void draw_camera(Studio *s,Object *original)
{
    Object value=*original;project_camera_position(&s->project,original,value.position);Object *o=&value;
    gboolean selected=s->selected>=0 && g_ptr_array_index(s->project.objects,s->selected)==original;
    box(s,o,selected,FALSE);
    double yaw=o->rotation[1]*G_PI/180,pitch=o->rotation[0]*G_PI/180;
    double out[]={sin(yaw)*cos(pitch),sin(pitch),cos(yaw)*cos(pitch)};
    double right[]={cos(yaw),0,-sin(yaw)},down[]={sin(yaw)*sin(pitch),-cos(pitch),cos(yaw)*sin(pitch)};
    double corners[4][3];
    for(int v=0;v<4;v++) {
        for(int k=0;k<3;k++) corners[v][k]=o->position[k]-out[k]*80+right[k]*(v&1?40:-40)+down[k]*(v&2?30:-30);
        line(s,o->position,corners[v],selected?1:.2,selected?.85:.8,selected?.2:1);
    }
    for(int v=0;v<4;v++) line(s,corners[v],corners[v^1],selected?1:.2,selected?.85:.8,selected?.2:1);
    line(s,corners[0],corners[2],selected?1:.2,selected?.85:.8,selected?.2:1);
    line(s,corners[1],corners[3],selected?1:.2,selected?.85:.8,selected?.2:1);
}

static gboolean render(GtkGLArea *area, GdkGLContext *context, Studio *s)
{
    (void)context;
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    glViewport(0, 0, gtk_widget_get_width(GTK_WIDGET(area))*scale, gtk_widget_get_height(GTK_WIDGET(area))*scale);
    glClearColor(.055, .075, .10, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!s->program) return TRUE;
    glUseProgram(s->program); glBindVertexArray(s->vao); glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,6*sizeof(GLfloat),NULL);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,6*sizeof(GLfloat),(void*)(4*sizeof(GLfloat)));
    for (int i = -10; i <= 10; i++) {
        int u=s->project.drag_axis==0?1:0,v=s->project.drag_axis==2?1:2;
        double a[3]={0},b[3]={0},c[3]={0},d[3]={0};
        a[u]=b[u]=i*64;a[v]=-640;b[v]=640;
        c[v]=d[v]=i*64;c[u]=-640;d[u]=640;
        line(s, a, b, .14, .20, .25); line(s, c, d, .14, .20, .25);
    }
    for (guint i = 0; i < s->project.objects->len; i++) {
        Object *o = g_ptr_array_index(s->project.objects, i);
        if(o->kind==OBJECT_CAMERA) {draw_camera(s,o);continue;}
        if(!o->visual_attempted && (*o->asset || *o->texture)) {
            GError *error=NULL;o->visual=asset_load(&s->project,o,&error);o->visual_attempted=TRUE;
            if(error) {studio_status(s,error->message);g_error_free(error);}
        }
        if(o->visual) draw_asset(s,o);else box(s,o,(int)i==s->selected,FALSE);
        if (s->project.show_colliders) box(s, o, FALSE, TRUE);
    }
    /* Selection is an overlay after every object, so textured geometry and
     * overlapping objects cannot hide it. Bounds follow the actual asset. */
    if (s->selected>=0 && (guint)s->selected<s->project.objects->len) {
        Object *o=g_ptr_array_index(s->project.objects,s->selected);
        GLfloat widths[2]; glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE,widths);
        glLineWidth(MIN(2.0f,widths[1]));
        Object copy=*o;project_camera_position(&s->project,o,copy.position);box(s,&copy,TRUE,FALSE);
        glLineWidth(1);
    }
    double origin[] = {0,0,0}, x[] = {160,0,0}, y[] = {0,160,0}, z[] = {0,0,160};
    const double *axis_ends[]={x,y,z};
    static const float axis_colors[][3]={{1,.38,.32},{.45,1,.48},{.4,.72,1}};
    for(int i=0;i<3;i++) {
        axis_stroke(s,origin,axis_ends[i],4,axis_colors[i][0],axis_colors[i][1],axis_colors[i][2]);
    }
    if(s->project.show_game_frame) {
        double w=gtk_widget_get_width(s->viewport),h=gtk_widget_get_height(s->viewport);
        GLfloat frame[]={-320/w,-256/h,0,1,0,0, 320/w,-256/h,0,1,0,0, 320/w,256/h,0,1,0,0, -320/w,256/h,0,1,0,0};
        glUniform1i(glGetUniformLocation(s->program,"textured"),0);glUniform4f(glGetUniformLocation(s->program,"tint"),.65,.65,.65,1);
        glBufferData(GL_ARRAY_BUFFER,sizeof(frame),frame,GL_STREAM_DRAW);glDrawArrays(GL_LINE_LOOP,0,4);
    }
    glBindVertexArray(0); glUseProgram(0);
    return TRUE;
}

static void drag_begin(GtkGestureDrag *gesture, double x, double y, Studio *s)
{
    (void)gesture;
    int hit = -1;
    double best = 45;
    for (guint i = 0; i < s->project.objects->len; i++) {
        Object *o = g_ptr_array_index(s->project.objects, i);
        double px, py;
        double position[3];project_camera_position(&s->project,o,position);
        if(!viewport_project(s, position, &px, &py)) continue;
        double distance = hypot(px-x, py-y);
        if (distance < best) { best = distance; hit = i; }
    }
    s->history_key=NULL;
    s->selected = hit; s->dragging = hit >= 0;
    if (hit >= 0) {
        Object *o = g_ptr_array_index(s->project.objects, hit);
        project_camera_position(&s->project,o,s->drag_start);
        PsxCamera c=camera(s);
        s->drag_screen_x=x;s->drag_screen_y=y;
        if(!psx_camera_plane_axis(&c,x-gtk_widget_get_width(s->viewport)/2.0,
            y-gtk_widget_get_height(s->viewport)/2.0,s->project.drag_axis,s->drag_start[s->project.drag_axis],s->drag_anchor)) s->dragging=FALSE;
        gtk_widget_grab_focus(s->viewport);
    }
    if(s->dragging) studio_checkpoint(s,NULL);
    studio_refresh(s);
}

static void drag_update(GtkGestureDrag *gesture, double dx, double dy, Studio *s)
{
    (void)gesture;
    if (!s->dragging || s->selected < 0) return;
    Object *o = g_ptr_array_index(s->project.objects, s->selected);
    PsxCamera c=camera(s);double point[3];
    if(!psx_camera_plane_axis(&c,s->drag_screen_x+dx-gtk_widget_get_width(s->viewport)/2.0,
        s->drag_screen_y+dy-gtk_widget_get_height(s->viewport)/2.0,s->project.drag_axis,s->drag_start[s->project.drag_axis],point)) return;
    o->position[s->project.drag_axis]=s->drag_start[s->project.drag_axis];
    for(int k=0;k<3;k++) if(k!=s->project.drag_axis) o->position[k]=CLAMP(s->drag_start[k]+point[k]-s->drag_anchor[k],-32767,32767);
    if(o->kind==OBJECT_CAMERA && o->follow_target) for(guint i=0;i<s->project.objects->len;i++) {Object *target=g_ptr_array_index(s->project.objects,i);
        if(target->id==o->follow_target) for(int k=0;k<3;k++) o->follow_offset[k]=o->position[k]-target->position[k];}
    s->project.dirty = TRUE;
    studio_refresh(s);
}

static void drag_end(GtkGestureDrag *gesture, double dx, double dy, Studio *s)
{ (void)gesture; (void)dx; (void)dy; s->dragging = FALSE; }

static void orbit_begin(GtkGestureDrag *gesture, double x, double y, Studio *s)
{
    (void)gesture; (void)x; (void)y;
    gtk_widget_grab_focus(s->viewport);
    studio_checkpoint(s,NULL);
    s->orbit_yaw = s->project.view[0];
    s->orbit_pitch = s->project.view[1];
    gtk_widget_set_cursor_from_name(s->viewport, "grabbing");
}

static void orbit_update(GtkGestureDrag *gesture, double dx, double dy, Studio *s)
{
    (void)gesture;
    double yaw = remainder(s->orbit_yaw + dx * .35, 360);
    double pitch = CLAMP(s->orbit_pitch + dy * .35, -90, 90);
    if (yaw == s->project.view[0] && pitch == s->project.view[1]) return;
    s->project.view[0] = yaw; s->project.view[1] = pitch;
    s->project.dirty = TRUE;
    studio_refresh(s);
}

static void orbit_end(GtkGestureDrag *gesture, double dx, double dy, Studio *s)
{
    (void)gesture; (void)dx; (void)dy;
    gtk_widget_set_cursor_from_name(s->viewport, NULL);
}

static gboolean scroll(GtkEventControllerScroll *controller, double dx, double dy, Studio *s)
{
    static char pan_history;
    GdkModifierType state=gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
    gboolean pan=(state&GDK_SHIFT_MASK)!=0;
    /* Some devices translate Shift+wheel into horizontal scroll deltas. */
    double delta=pan && dy==0?dx:dy;
    if(delta==0) return FALSE;
    studio_checkpoint(s,pan?(gpointer)&pan_history:(gpointer)controller);
    if(pan) {
        PsxCamera c=camera(s);
        double distance=delta*32/c.zoom;
        /* Camera-local right is independent of pitch; translate, never orbit. */
        double right[3]={c.cy,0,-c.sy};
        for(int k=0;k<3;k++) if(fabs(right[k])>1e-9) {
            double a=(-32767-s->project.camera[k])/right[k];
            double b=(32767-s->project.camera[k])/right[k];
            distance=CLAMP(distance,MIN(a,b),MAX(a,b));
        }
        for(int k=0;k<3;k++) s->project.camera[k]+=right[k]*distance;
    } else s->project.view[2]=CLAMP(s->project.view[2]*exp(-delta*.15),.15,6);
    s->project.dirty=TRUE;
    studio_refresh(s);
    gtk_gl_area_queue_render(GTK_GL_AREA(s->viewport));
    return TRUE;
}

GtkWidget *viewport_create(Studio *s)
{
    GtkWidget *area = gtk_gl_area_new();
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area), GDK_GL_API_GL | GDK_GL_API_GLES);
    /* GDK clamps desktop GL to its 3.2 minimum; ANGLE uses GLES 3.0. */
    gtk_gl_area_set_required_version(GTK_GL_AREA(area), 3, 0);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(area),TRUE);
    gtk_widget_set_focusable(area,TRUE);
    gtk_widget_set_hexpand(area, TRUE); gtk_widget_set_vexpand(area, TRUE);
    g_signal_connect(area, "realize", G_CALLBACK(realize), s);
    g_signal_connect(area, "unrealize", G_CALLBACK(unrealize), s);
    g_signal_connect(area, "render", G_CALLBACK(render), s);
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), s);
    g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), s);
    g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), s);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));
    GtkGesture *orbit = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(orbit), GDK_BUTTON_SECONDARY);
    g_signal_connect(orbit, "drag-begin", G_CALLBACK(orbit_begin), s);
    g_signal_connect(orbit, "drag-update", G_CALLBACK(orbit_update), s);
    g_signal_connect(orbit, "drag-end", G_CALLBACK(orbit_end), s);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(orbit));
    GtkEventController *wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(wheel, "scroll", G_CALLBACK(scroll), s);
    gtk_widget_add_controller(area, wheel);
    return area;
}
