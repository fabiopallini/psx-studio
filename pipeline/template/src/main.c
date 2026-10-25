#include "psx.h"

static PsxObject objects[3];
static PsxScene scene;

static void player_update(PsxObject *object, PsxScene *current, unsigned long input)
{
    long dx;
    long dz;
    dx = 0;
    dz = 0;
    if (input & PADLleft) dx -= 2;
    if (input & PADLright) dx += 2;
    if (input & PADLup) dz -= 2;
    if (input & PADLdown) dz += 2;
    psx_object_move(object, current, dx, 0, dz);
}

static void player_collision(PsxObject *object, PsxObject *other, PsxScene *current)
{
    (void)current;
    if (other->id == 3) psx_object_kill(object);
}

static const PsxLogic player_logic = { 0, player_update, player_collision };

int main(void)
{
    unsigned int i;
    int axis;
    int fps;
    scene.objects = objects;
    scene.count = 3;
    for (i = 0; i < scene.count; i++) {
        objects[i].id = i + 1;
        objects[i].alive = 1;
        objects[i].kind = PSX_OBJECT_SPRITE;
        objects[i].solid = 1;
        for (axis = 0; axis < 3; axis++) {
            objects[i].half_size[axis] = 10;
            objects[i].scale[axis] = 4096;
        }
    }
    objects[0].position[0] = -80;
    objects[0].logic = &player_logic;
    objects[1].half_size[2] = 40;
    objects[2].position[0] = 80;
    objects[2].solid = 0;
    psInit();
    psx_scene_init(&scene);
    scene.yaw=359;scene.pitch=391;scene.zoom=3686;
    psSetScene(&scene,0,0);
    while (1) {
        psUpdate();
        if(!psBeginFrame()) continue;
        psDrawScene();
        FntPrint("PSX STUDIO - C89 PIPELINE TEST\n");
        fps=psFrameRate();
        if(fps<0) FntPrint("FPS: --\n");
        else FntPrint("FPS: %d.%d\n",fps/10,fps%10);
        FntPrint("D-PAD: MOVE BLUE PLAYER\n");
        FntPrint("GREY: WALL  ORANGE: DEATH TRIGGER\n");
        if (!objects[0].alive) FntPrint("PLAYER DEAD - RESET TO RESTART\n");
        psDisplay();
    }
    return 0;
}
