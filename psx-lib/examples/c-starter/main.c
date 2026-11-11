#include "psx.h"

/* A complete C89 program: no .psx document, generated objects or game HUD. */
static PsxScene scene;
static int x=140,y=110;

static void update(PsxScene *current,unsigned long input)
{
    (void)current;
    if(input&32768UL) x-=2;
    if(input&8192UL) x+=2;
    if(input&4096UL) y-=2;
    if(input&16384UL) y+=2;
    if(x<0) x=0;
    if(x>300) x=300;
    if(y<0) y=0;
    if(y>236) y=236;
}

static void draw(PsxScene *current)
{
    psx_overlay_clear(current);
    psx_overlay_rect(current,0,0,320,256,12,18,28);
    psx_overlay_rect(current,x,y,20,20,100,170,210);
    psx_overlay_text(current,"C SOURCE PROJECT\n\nD-PAD: MOVE");
}

int main(void)
{
    scene.update_game=update;
    scene.prepare_overlay=draw;
    scene.zoom=4096;
    psInit();psx_scene_init(&scene);
    if(!psSetScene(&scene,0,0)) return 1;
    while(1) {
        psUpdate();
        if(!psBeginFrame()) continue;
        psDrawScene();psDrawOverlay();psDisplay();
    }
    return 0;
}
