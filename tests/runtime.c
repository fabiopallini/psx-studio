#include "../runtime/object.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int updates;
static int collisions;

static void update(PsxObject *object, PsxScene *scene, unsigned long input)
{
    (void)object;
    (void)scene;
    (void)input;
    updates++;
}

static void collision(PsxObject *object, PsxObject *other, PsxScene *scene)
{
    (void)object;
    (void)other;
    (void)scene;
    collisions++;
}

static void arm_collision(PsxObject *object,PsxScene *scene,unsigned long input)
{(void)scene;(void)input;object->collision_action=1;}

int main(void)
{
    PsxObject objects[3];
    PsxScene scene;
    PsxLogic logic;
    int i;
    int axis;
    memset(objects, 0, sizeof(objects));
    memset(&scene,0,sizeof(scene));
    logic.init = 0;
    logic.update = update;
    logic.collision = collision;
    scene.objects = objects;
    scene.count = 3;
    for (i = 0; i < 3; i++) {
        objects[i].id = (unsigned int)i + 1;
        objects[i].alive = 1;
        objects[i].solid = 1;
        objects[i].position[0] = i * 40;
        objects[i].logic = &logic;
        for (axis = 0; axis < 3; axis++) objects[i].half_size[axis] = 10;
    }
    assert(!psx_objects_overlap(&objects[0], &objects[1]));
    assert(psx_object_move(&objects[0], &scene, 20, 0, 0));
    assert(!psx_object_move(&objects[0], &scene, 1, 0, 0));
    assert(objects[0].position[0] == 20);
    assert(collisions == 2);
    assert(!psx_object_move(&objects[0], &scene, LONG_MAX, 0, 0));
    assert(objects[0].position[0] == 20);
    objects[1].solid = 0;
    assert(psx_object_move(&objects[0], &scene, 1, 0, 0));
    collisions = 0;
    psx_scene_init(&scene);
    psx_scene_update(&scene, 42UL);
    assert(updates == 3);
    assert(collisions == 2);
    psx_object_kill(&objects[1]);
    assert(!psx_objects_overlap(&objects[0], &objects[1]));
    psx_scene_update(&scene, 42UL);
    assert(updates == 5);
    assert(collisions == 2);
    objects[0].input_mode = 1;
    objects[0].speed = 3;
    psx_scene_update(&scene, 8192UL);
    assert(objects[0].position[0] == 24);
    objects[0].input_mode = 0;
    objects[1].alive = 1;
    objects[1].collision_action = 2;
    objects[0].position[0] = 0;
    assert(!psx_object_move(&objects[0], &scene, 100, 0, 0));
    assert(!objects[0].alive);
    assert(objects[0].position[0] == 21);
    /* Collision actions enabled by an update must still run in this frame. */
    memset(objects,0,sizeof(objects));memset(&scene,0,sizeof(scene));
    scene.objects=objects;scene.count=2;
    for(i=0;i<2;i++) {objects[i].alive=1;for(axis=0;axis<3;axis++) objects[i].half_size[axis]=10;}
    logic.update=arm_collision;logic.collision=0;objects[0].logic=&logic;
    psx_scene_update(&scene,0);assert(!objects[0].alive && objects[1].alive);
    puts("C89 runtime: blocking movement, bounds, triggers, callbacks and death passed.");
    return 0;
}
