#include "../runtime/object.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int updates;
static void game_update(PsxScene *scene,unsigned long input)
{(void)scene;if(input==64UL) updates++;}

int main(void)
{
    static PsxScene scene;
    char text[700];
    unsigned int i;
    scene.update_game=game_update;
    psx_scene_update(&scene,64UL);assert(updates==1); /* No object/controller module required. */
    psx_overlay_clear(&scene);
    assert(psx_overlay_rect(&scene,2,4,16,20,-1,300,90));
    assert(scene.overlay.commands[0].r==0 && scene.overlay.commands[0].g==255);
    assert(!psx_overlay_rect(&scene,0,0,0,10,0,0,0));
    assert(!psx_overlay_sprite(&scene,0,0,0,40,40,INT_MAX,0,2,2));
    assert(!psx_overlay_sprite(&scene,0,1000,0,40,40,0,0,64,64));
    for(i=0;i<PSX_OVERLAY_SPRITES;i++) assert(psx_overlay_sprite(&scene,0,0,0,40,40,0,0,64,64));
    assert(!psx_overlay_sprite(&scene,0,0,0,40,40,0,0,64,64));
    while(scene.overlay.count<PSX_OVERLAY_CAPACITY) assert(psx_overlay_rect(&scene,0,0,1,1,0,0,0));
    assert(!psx_overlay_rect(&scene,0,0,1,1,0,0,0));
    memset(text,'x',sizeof(text)-1);text[sizeof(text)-1]=0;
    psx_overlay_text(&scene,text);assert(strlen(scene.overlay.text)==PSX_OVERLAY_TEXT_SIZE-1);
    psx_overlay_clear(&scene);
    assert(scene.overlay.count==0 && !scene.overlay.text[0]);
    psx_scene_clear(&scene);assert(!scene.prepare_overlay && !scene.update_game);
    puts("C89 overlay: bounded commands, UVs, text, custom updates passed.");
    return 0;
}
