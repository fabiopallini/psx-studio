#include "scene.h"
#include <assert.h>
#include <string.h>
extern void ward_reset(void);
static PsxObject *object(PsxScene *s,unsigned int id)
{
    unsigned int i;
    for(i=0;i<s->count;i++) if(s->objects[i].id==id) return &s->objects[i];
    assert(0);return 0;
}
static PsxScene *isolated(void)
{
    PsxScene *s;int i;
    ward_reset();s=studio_scene_load_id(2);
    for(i=0;i<4;i++) {object(s,20U+(unsigned int)i)->position[0]=1000;object(s,20U+(unsigned int)i)->position[2]=1000;}
    return s;
}
static void shot(PsxScene *s)
{
    int i;
    psx_scene_update(s,8UL|64UL);
    for(i=0;i<24;i++) psx_scene_update(s,8UL);
}
int main(void)
{
    PsxScene *s;
    PsxObject *p,*enemy;
    const PsxMesh *rest;
    long x,z,diagonal;
    int i,g;
    s=studio_scene_load();assert(s->scene_id==1 && s->background_texture && s->hud.style==PSX_HUD_MENU);
    psx_scene_update(s,64UL);assert(s->pending_scene==2);assert(studio_scene_apply_change());
    assert(s->background_texture && studio_texture_count==6);
    for(i=10;i<=14;i++) assert(object(s,(unsigned int)i)->solid && object(s,(unsigned int)i)->alive &&
        (object(s,(unsigned int)i)->render_flags&PSX_RENDER_HIDDEN));
    assert(object(s,200)->follow_target==0 && s->projection==1);
    s=isolated();p=object(s,2);rest=p->mesh;x=p->position[0];z=p->position[2];
    for(i=0;i<10;i++) psx_scene_update(s,4096UL);
    assert(p->position[0]==x && p->position[2]==z-20);
    assert(object(s,200)->position[0]==0 && object(s,200)->position[1]==300 && object(s,200)->position[2]==580);
    s=isolated();p=object(s,2);p->position[0]=p->position[2]=0;p->rotation[1]=512;
    for(i=0;i<50;i++) psx_scene_update(s,4096UL);
    diagonal=p->position[0]*p->position[0]+p->position[2]*p->position[2];
    assert(diagonal>=9600 && diagonal<=10200);
    s=isolated();p=object(s,2);x=p->position[0];z=p->position[2];
    psx_scene_update(s,8UL|4096UL);assert(p->position[0]==x && p->position[2]==z && p->mesh==object(s,50)->mesh);
    psx_scene_update(s,8UL|64UL);assert(s->hud.ammo==35 && object(s,100)->alive && object(s,80)->alive);
    assert(object(s,100)->position[1]==52);
    for(i=0;i<15;i++) psx_scene_update(s,8UL|64UL);
    assert(s->hud.ammo==35 && !object(s,80)->alive); /* Edge-triggered fire. */
    psx_scene_update(s,8UL);psx_scene_update(s,8UL|64UL);assert(s->hud.ammo==34);
    s=isolated();p=object(s,2);p->position[0]=0;p->position[2]=160;enemy=object(s,20);
    enemy->position[0]=0;enemy->position[2]=-10;
    shot(s);assert(enemy->solid);shot(s);assert(enemy->solid);shot(s);
    assert(enemy->alive && !enemy->solid && enemy->position[1]==0 && enemy->mesh==object(s,62)->mesh);
    assert(enemy->mesh->count==object(s,60)->mesh->count);
    s->hud.health=100;p->position[0]=enemy->position[0];p->position[2]=enemy->position[2];
    for(i=0;i<80;i++) psx_scene_update(s,0);
    assert(s->hud.health==100); /* Corpse cannot attack or block movement. */
    s=isolated();enemy=object(s,20);enemy->position[0]=0;enemy->position[2]=-10;enemy->position[1]=300;
    for(i=0;i<3;i++) shot(s);
    assert(enemy->solid); /* Real Y hit volumes, not screen-space hits. */
    s=isolated();p=object(s,2);p->position[0]=p->position[2]=0;p->rotation[1]=1024;
    enemy=object(s,20);enemy->position[0]=-260;enemy->position[2]=0;
    for(i=0;i<3;i++) shot(s);
    assert(enemy->solid && !object(s,100)->alive); /* Invisible desk collider shields it. */
    s=isolated();p=object(s,2);p->position[0]=-195;p->position[2]=0;p->rotation[1]=1024;
    for(i=0;i<40;i++) psx_scene_update(s,4096UL);
    assert(p->position[0]>=-198); /* Hidden collision volume remains solid. */
    s=isolated();p=object(s,2);enemy=object(s,20);enemy->position[0]=p->position[0];enemy->position[2]=p->position[2]-20;
    psx_scene_update(s,0);assert(s->hud.health==80);
    for(i=0;i<10;i++) psx_scene_update(s,0);
    assert(s->hud.health==80);
    s=isolated();p=object(s,2);
    for(g=0;g<4;g++) {
        enemy=object(s,20U+(unsigned int)g);enemy->position[0]=0;enemy->position[2]=-10;
        for(i=0;i<3;i++) shot(s);
        assert(!enemy->solid && enemy->alive);
    }
    psx_scene_update(s,0);assert(s->hud.style==PSX_HUD_COMBAT);
    psx_scene_update(s,2048UL);assert(s->pending_scene==2);assert(studio_scene_apply_change());
    assert(s->hud.health==100 && s->hud.ammo==36 && object(s,20)->solid && object(s,2)->mesh==rest);
    /* Blood starts only on impact, stays at the hit location, and expires. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    p->position[0]=0;p->position[2]=100;enemy->position[0]=0;enemy->position[2]=0;
    assert(!object(s,81)->alive);
    psx_scene_update(s,8UL|64UL);
    for(i=0;i<12 && !object(s,81)->alive;i++) psx_scene_update(s,8UL);
    assert(object(s,81)->alive && object(s,81)->kind==PSX_OBJECT_BILLBOARD);
    assert(object(s,81)->position[1]==52 && !object(s,82)->alive);
    x=object(s,81)->position[0];z=object(s,81)->position[2];
    for(i=0;i<20;i++) psx_scene_update(s,8UL);
    assert(!object(s,81)->alive && object(s,81)->position[0]==x && object(s,81)->position[2]==z);
    /* Editable trigger transform, bounds, offset and destination drive travel.
     * Other actors overlapping it must not activate the engine's default action. */
    s=isolated();p=object(s,2);enemy=object(s,30);
    assert(enemy->alive && !enemy->solid && !enemy->collision_action &&
        (enemy->render_flags&PSX_RENDER_HIDDEN) && enemy->target_scene==3);
    object(s,20)->position[0]=enemy->position[0];object(s,20)->position[2]=enemy->position[2];
    psx_scene_update(s,0);assert(!s->pending_scene);
    enemy->position[0]=60;enemy->position[2]=-70;
    enemy->half_size[0]=5;enemy->half_size[2]=10;enemy->collider_offset[0]=20;
    p->position[0]=0;p->position[2]=-325;psx_scene_update(s,0);assert(!s->pending_scene);
    p->position[0]=60;p->position[2]=-70;psx_scene_update(s,0);assert(!s->pending_scene);
    enemy->target_scene=1;p->position[0]=80;
    psx_scene_update(s,0);assert(s->pending_scene==1);
    assert(studio_scene_apply_change() && s->scene_id==1);
    /* Door proximity, return path, no automatic bounce, and persistent combat. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    enemy->position[0]=0;enemy->position[2]=-10;
    for(i=0;i<3;i++) shot(s);
    assert(!enemy->solid);s->hud.health=80;
    p->position[0]=100;p->position[2]=-325;psx_scene_update(s,0);assert(!s->pending_scene);
    p->position[0]=0;p->position[2]=-210;p->rotation[1]=0;
    for(i=0;i<70 && !s->pending_scene;i++) psx_scene_update(s,4096UL);
    assert(s->pending_scene==3); /* Door remains reachable through the rear wall. */
    assert(studio_scene_apply_change());assert(s->scene_id==3 && s->background_texture);
    assert(s->hud.health==80 && s->hud.ammo==33 && object(s,20)->solid);
    /* Room 3 has its own patients, not the atrium corpse state. */
    p=object(s,2);assert(p->position[2]==-330 && p->rotation[1]==2048);
    psx_scene_update(s,0);assert(!s->pending_scene);
    p->position[2]=-385;psx_scene_update(s,0);assert(s->pending_scene==2);
    assert(studio_scene_apply_change());assert(s->scene_id==2 && !object(s,20)->solid);
    assert(object(s,20)->mesh==object(s,62)->mesh && s->hud.health==80 && s->hud.ammo==33);
    psx_scene_update(s,2048UL);assert(studio_scene_apply_change());
    assert(s->scene_id==2 && s->hud.health==100 && s->hud.ammo==36 && object(s,20)->solid);
    s->hud.health=0;psx_scene_update(s,0);assert(strstr(s->hud.message,"YOU ARE DOWN"));
    psx_scene_update(s,2048UL);assert(studio_scene_apply_change());assert(s->hud.health==100);
    return 0;
}
