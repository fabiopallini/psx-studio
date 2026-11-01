#include "studio.h"
#include <string.h>

/* Picking does not need a GL context, only the widget's logical dimensions. */
void studio_status(Studio *s,const char *message) {(void)s;(void)message;}
void studio_refresh(Studio *s) {(void)s;}
void studio_checkpoint(Studio *s,gpointer key) {(void)s;(void)key;}

static Studio s;
static void reset(int projection)
{
    project_clear(&s.project);project_init(&s.project);
    s.project.view[0]=s.project.view[1]=0;s.project.view[2]=1;
    s.project.projection=projection;
}
static int pick(double x,double y)
{ return viewport_pick(&s,gtk_widget_get_width(s.viewport)/2.0+x,gtk_widget_get_height(s.viewport)/2.0+y); }

static void surfaces(void)
{
    for(int projection=0;projection<2;projection++) {
        reset(projection);
        Object *box=project_add(&s.project,OBJECT_MESH);
        box->scale[0]=8;box->scale[1]=3;
        /* Far outside the old 45-pixel centre radius, but inside the face. */
        g_assert_cmpint(pick(140,45),==,0);
        g_assert_cmpint(pick(230,0),==,-1);
        Object *front=project_add(&s.project,OBJECT_MESH);front->position[2]=100;front->scale[0]=front->scale[1]=3;
        g_assert_cmpint(pick(20,10),==,1);
        /* Array order must not override nearest geometry. */
        gpointer tmp=g_ptr_array_index(s.project.objects,0);
        g_ptr_array_index(s.project.objects,0)=front;g_ptr_array_index(s.project.objects,1)=tmp;
        g_assert_cmpint(pick(20,10),==,0);
        front->position[0]=400;
        box->rotation[2]=90;
        g_assert_cmpint(pick(0,140),==,1);
        g_assert_cmpint(pick(140,0),==,-1);
    }
}
static void sprites(void)
{
    for(int projection=0;projection<2;projection++) {
        reset(projection);
        Object *back=project_add(&s.project,OBJECT_MESH);back->scale[0]=back->scale[1]=5;
        Object *sprite=project_add(&s.project,OBJECT_BILLBOARD);
        sprite->position[2]=100;sprite->scale[0]=sprite->scale[1]=4;
        sprite->visual=asset_load(&s.project,sprite,NULL);sprite->visual_attempted=TRUE;
        Asset *a=sprite->visual;a->width=a->height=2;a->pixels=g_malloc0(16);
        /* The right half is opaque; left half lets the rear object through. */
        a->pixels[7]=a->pixels[15]=255;
        g_assert_cmpint(pick(-55,10),==,0);
        g_assert_cmpint(pick(55,10),==,1);
        sprite->scale[0]=-4;
        g_assert_cmpint(pick(-55,10),==,1);
        g_assert_cmpint(pick(55,10),==,0);
        /* Billboard orientation follows the view, including pitch and yaw. */
        s.project.view[0]=35;s.project.view[1]=25;
        double x,y;g_assert_true(viewport_project(&s,sprite->position,&x,&y));
        g_assert_cmpint(viewport_pick(&s,x-25,y+5),==,1);
    }
}
static void wire_helpers(void)
{
    reset(PSX_PROJECTION_PERSPECTIVE);
    Object *helper=project_add(&s.project,OBJECT_MESH);
    helper->position[2]=200;helper->scale[0]=helper->scale[1]=8;
    Object *actor=project_add(&s.project,OBJECT_MESH);
    actor->visual=asset_load(&s.project,actor,NULL);actor->visual_attempted=TRUE;
    g_assert_cmpint(pick(0,0),==,1);
    g_assert_cmpint(pick(100,0),==,0);
}
static void clipping(void)
{
    reset(PSX_PROJECTION_PERSPECTIVE);
    Object *box=project_add(&s.project,OBJECT_MESH);box->position[2]=700;
    g_assert_cmpint(pick(0,0),==,-1);
    box->position[2]=560; /* The front face crosses the near plane. */
    g_assert_cmpint(pick(0,0),==,0);
    box->position[2]=-300000;
    g_assert_cmpint(pick(0,0),==,-1);
}
int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL);
    if(!gtk_init_check()) {g_print("SKIP viewport picking: no isolated GUI display available\n");return 0;}
    s.viewport=gtk_drawing_area_new();g_object_ref_sink(s.viewport);
    gtk_widget_allocate(s.viewport,800,600,-1,NULL);
    project_init(&s.project);
    g_test_add_func("/viewport/surfaces",surfaces);
    g_test_add_func("/viewport/sprite-alpha",sprites);
    g_test_add_func("/viewport/clipping",clipping);
    g_test_add_func("/viewport/wire-helpers",wire_helpers);
    int result=g_test_run();project_clear(&s.project);g_object_unref(s.viewport);return result;
}
