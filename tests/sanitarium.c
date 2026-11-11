#include "scene.h"
#include <assert.h>
#include <string.h>
extern int sanitarium_health,sanitarium_ammo;
extern int sanitarium_pickup_collect(unsigned int scene,unsigned int object);
extern int sanitarium_inventory_at(int slot);
int main(void)
{
    PsxScene *s;
    unsigned int id;
    int ammo;
    long x,z;
    s=studio_scene_load_id(1);assert(s && s->prepare_overlay);
    s->prepare_overlay(s);assert(strstr(s->overlay.text,"SANITARIUM"));
    psx_scene_update(s,64UL);assert(s->pending_scene==2);
    assert(studio_scene_apply_change());s=studio_scene_load_id(2);
    assert(s && s->prepare_overlay);
    s->prepare_overlay(s);assert(strstr(s->overlay.text,"HP 100"));
    x=s->objects[0].position[0];z=s->objects[0].position[2];
    psx_scene_update(s,16UL);s->prepare_overlay(s);
    assert(strstr(s->overlay.text,"INVENTORY"));
    assert(s->overlay.count>6 && s->overlay.count<=PSX_OVERLAY_CAPACITY);
    psx_scene_update(s,4096UL);s->prepare_overlay(s);
    assert(s->objects[0].position[0]==x && s->objects[0].position[2]==z);
    psx_scene_update(s,0);psx_scene_update(s,16UL);psx_scene_update(s,0);
    ammo=sanitarium_ammo;psx_scene_update(s,72UL);
    assert(sanitarium_ammo==ammo-1);
    assert(sanitarium_pickup_collect(2,31)==1);
    assert(sanitarium_inventory_at(2)==3);
    sanitarium_health=60;
    for(id=3;id<=6;id++) {
        s=studio_scene_load_id(id);assert(s && s->prepare_overlay);
        s->prepare_overlay(s);assert(strstr(s->overlay.text,"HP  60"));
        assert(sanitarium_ammo==ammo-1 && sanitarium_inventory_at(2)==3);
    }
    sanitarium_health=0;psx_scene_update(s,0);s->prepare_overlay(s);
    assert(strstr(s->overlay.text,"YOU ARE DOWN"));
    psx_scene_update(s,2048UL);assert(s->pending_scene==2);
    assert(sanitarium_health==100 && sanitarium_ammo==36);
    assert(sanitarium_inventory_at(2)==0);
    return 0;
}
