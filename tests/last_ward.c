#include "scene.h"
#include <assert.h>
#include <string.h>
extern void ward_reset(void);
extern int ward_inventory_at(int slot);
extern int ward_inventory_has(int item);
extern int ward_inventory_add(int item);
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
static void press(PsxScene *s,unsigned long mask)
{
    psx_scene_update(s,0);psx_scene_update(s,mask);
}
static void collect_key(PsxScene *s)
{
    PsxObject *p;
    p=object(s,2);p->position[0]=-190;p->position[2]=30;
    press(s,64UL);
    assert(ward_inventory_has(3) && !object(s,31)->alive);
    assert(strstr(s->hud.message,"picked up"));
    press(s,64UL); /* Dismiss pickup message. */
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
    assert(s->background_texture && studio_texture_count==7);
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
    /* Contact cannot pin an already-overlapping player moving away. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    p->position[0]=p->position[2]=0;enemy->position[0]=0;enemy->position[2]=-18;
    psx_scene_update(s,0);assert(s->hud.health==80);
    for(i=0;i<10;i++) psx_scene_update(s,16384UL);
    assert(p->position[2]==10 && s->hud.health==80 && enemy->solid);
    /* Rounded contacts allow a diagonal escape past an AABB corner. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    p->position[0]=p->position[2]=0;p->rotation[1]=2560;
    enemy->position[0]=enemy->position[2]=-16;
    for(i=0;i<20;i++) psx_scene_update(s,4096UL);
    assert(p->position[0]>20 && p->position[2]>20 && enemy->solid);
    /* Escape assistance never lets the survivor pass through walls/desks. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    p->position[0]=-195;p->position[2]=0;p->rotation[1]=1024;
    enemy->position[0]=-177;enemy->position[2]=0;psx_scene_update(s,0);
    for(i=0;i<20;i++) psx_scene_update(s,4096UL);
    assert(p->position[0]>=-198 && enemy->solid && object(s,14)->solid);
    /* Approaching bodies still block head-on movement; backing up can escape. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    p->position[0]=p->position[2]=0;enemy->position[0]=0;enemy->position[2]=-40;
    for(i=0;i<25;i++) psx_scene_update(s,4096UL);
    assert(p->position[2]-enemy->position[2]>=21);
    x=p->position[2];for(i=0;i<20;i++) psx_scene_update(s,16384UL);
    assert(p->position[2]>=x+20);
    /* Rear wall and floor stop the survivor before the painted doorway. */
    s=isolated();p=object(s,2);
    for(i=0;i<300;i++) psx_scene_update(s,4096UL);
    assert(p->position[2]==-218 && !s->pending_scene);
    psx_scene_update(s,0);press(s,64UL);assert(strstr(s->hud.message,"door is locked"));
    s=isolated();enemy=object(s,31);
    assert(enemy->kind==PSX_OBJECT_BILLBOARD && enemy->mesh->count==6);
    x=enemy->scale[0];for(i=0;i<15;i++) psx_scene_update(s,0);
    assert(enemy->scale[0]>x && (enemy->render_flags&PSX_RENDER_EMISSIVE));
    /* Interaction is explicit, and a locked door never loads another room. */
    s=isolated();p=object(s,2);psx_scene_update(s,0);
    assert(ward_inventory_at(0)==1 && ward_inventory_at(1)==2 && !ward_inventory_has(3));
    assert(object(s,31)->alive && object(s,100)->scale[0]==2048 && object(s,100)->scale[1]==2048);
    assert(object(s,100)->scale[2]==4096 && object(s,100)->half_size[0]==3);
    p->position[2]=-212;psx_scene_update(s,0);assert(!s->pending_scene);
    press(s,64UL);assert(!s->pending_scene && strstr(s->hud.message,"door is locked"));
    x=p->position[0];z=p->position[2];
    for(i=0;i<30;i++) psx_scene_update(s,64UL|4096UL);
    assert(!s->pending_scene && p->position[0]==x && p->position[2]==z);
    press(s,64UL); /* Close; merely remaining at the door cannot travel. */
    psx_scene_update(s,0);assert(!s->pending_scene);
    press(s,8UL|64UL);assert(s->hud.ammo==35 && !s->pending_scene);
    /* Inventory grid, selection, pause, pickup and contextual item use. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    press(s,64UL);assert(s->hud.ammo==36 && object(s,31)->alive); /* Cross alone is not fire. */
    press(s,16UL);assert(s->hud.style==PSX_HUD_INVENTORY && strstr(s->hud.message,"INVENTORY"));
    x=p->position[2];z=enemy->position[2];
    for(i=0;i<30;i++) psx_scene_update(s,4096UL|8UL|64UL);
    assert(p->position[2]==x && enemy->position[2]==z && s->hud.ammo==36 && s->hud.health==100);
    assert(s->inventory.selected==3);
    assert(s->inventory.icons[0]==1 && s->inventory.icons[1]==2 && s->inventory.icons[2]==0);
    assert(strstr(s->hud.message,"empty inventory"));
    assert(strlen(s->hud.message)<PSX_HUD_MESSAGE_SIZE);
    press(s,16UL);collect_key(s);assert(ward_inventory_at(2)==3 && ward_inventory_at(3)==0);
    press(s,16UL);press(s,4096UL);press(s,8192UL);press(s,8192UL);
    assert(s->inventory.selected==2 && s->inventory.icons[2]==3 && strstr(s->hud.message,"ward door"));
    /* Slot selection persists when reopening; slot 2 is the key. */
    press(s,64UL);assert(strstr(s->hud.message,"No matching lock"));
    press(s,32UL);assert(ward_inventory_has(3) && !s->pending_scene);
    /* Editable trigger transform, bounds, offset and destination drive travel.
     * Other actors overlapping it must not activate the engine's default action. */
    s=isolated();p=object(s,2);enemy=object(s,30);
    assert(enemy->alive && !enemy->solid && !enemy->collision_action &&
        (enemy->render_flags&PSX_RENDER_HIDDEN) && enemy->target_scene==3);
    object(s,20)->position[0]=enemy->position[0];object(s,20)->position[2]=enemy->position[2];
    psx_scene_update(s,0);assert(!s->pending_scene);
    enemy->position[0]=60;enemy->position[2]=-70;
    enemy->half_size[0]=5;enemy->half_size[2]=10;enemy->collider_offset[0]=20;
    p->position[0]=0;p->position[2]=-212;psx_scene_update(s,0);assert(!s->pending_scene);
    p->position[0]=60;p->position[2]=-70;psx_scene_update(s,0);assert(!s->pending_scene);
    enemy->target_scene=1;p->position[0]=80;
    assert(ward_inventory_add(3));press(s,64UL);assert(!s->pending_scene && strstr(s->hud.message,"used the Ward Key"));
    press(s,64UL);assert(s->pending_scene==1);
    assert(studio_scene_apply_change() && s->scene_id==1);
    /* Door proximity, return path, no automatic bounce, and persistent combat. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    enemy->position[0]=0;enemy->position[2]=-10;
    for(i=0;i<3;i++) shot(s);
    assert(!enemy->solid);s->hud.health=80;collect_key(s);
    p->position[0]=100;p->position[2]=-212;psx_scene_update(s,0);assert(!s->pending_scene);
    p->position[0]=0;p->position[2]=-210;p->rotation[1]=0;
    for(i=0;i<70;i++) psx_scene_update(s,4096UL);
    assert(!s->pending_scene);press(s,64UL);
    assert(strstr(s->hud.message,"used the Ward Key") && !s->pending_scene);
    press(s,64UL);assert(s->pending_scene==3); /* Door remains reachable through the rear wall. */
    assert(studio_scene_apply_change());assert(s->scene_id==3 && s->background_texture);
    assert(s->hud.health==80 && s->hud.ammo==33 && object(s,20)->solid);
    /* Room 3 has its own patients, not the atrium corpse state. */
    p=object(s,2);assert(p->position[2]==100 && p->rotation[1]==0);
    psx_scene_update(s,0);assert(!s->pending_scene);
    p->position[2]=190;psx_scene_update(s,0);assert(!s->pending_scene);
    assert(ward_inventory_has(3));press(s,64UL);assert(s->pending_scene==2);
    assert(studio_scene_apply_change());assert(s->scene_id==2 && !object(s,20)->solid);
    assert(object(s,20)->mesh==object(s,62)->mesh && s->hud.health==80 && s->hud.ammo==33);
    assert(!object(s,31)->alive && ward_inventory_has(3));
    assert(object(s,21)->position[0]==120 && object(s,21)->position[2]==-155);
    /* Living patients return to authored spawns; the corpse stays where killed. */
    assert(object(s,20)->position[2]!=-145 && object(s,20)->position[1]==0);
    psx_scene_update(s,2048UL);assert(studio_scene_apply_change());
    assert(s->scene_id==2 && s->hud.health==100 && s->hud.ammo==36 && object(s,20)->solid);
    assert(!ward_inventory_has(3) && object(s,31)->alive);
    s->hud.health=0;psx_scene_update(s,0);assert(strstr(s->hud.message,"YOU ARE DOWN"));
    psx_scene_update(s,2048UL);assert(studio_scene_apply_change());assert(s->hud.health==100);
    /* Four playable rooms form a bidirectional chain with distinct cameras. */
    s=isolated();assert(ward_inventory_add(3));p=object(s,2);
    psx_scene_update(s,0);p->position[0]=0;p->position[2]=-212;
    press(s,64UL);press(s,64UL);assert(studio_scene_apply_change() && s->scene_id==3);
    p=object(s,2);psx_scene_update(s,0);p->position[2]=-272;
    press(s,64UL);assert(s->pending_scene==4 && studio_scene_apply_change());
    assert(s->scene_id==4 && object(s,200)->rotation[1]>0 && s->background_texture);
    p=object(s,2);p->position[0]=30;p->position[2]=10;
    press(s,64UL);assert(s->hud.ammo==60 && !object(s,35)->alive);
    press(s,64UL);p->position[0]=150;p->position[2]=-205;
    press(s,64UL);assert(s->pending_scene==5 && studio_scene_apply_change());
    assert(s->scene_id==5 && object(s,200)->rotation[1]<0 && s->background_texture);
    p=object(s,2);psx_scene_update(s,0);p->position[0]=-70;p->position[2]=-205;
    press(s,64UL);assert(studio_scene_apply_change() && s->scene_id==4);
    assert(!object(s,35)->alive && s->hud.ammo==60);
    p=object(s,2);psx_scene_update(s,0);p->position[0]=-110;p->position[2]=-205;
    press(s,64UL);assert(studio_scene_apply_change() && s->scene_id==3);
    p=object(s,2);psx_scene_update(s,0);p->position[0]=0;p->position[2]=190;
    press(s,64UL);assert(studio_scene_apply_change() && s->scene_id==2);
    assert(ward_inventory_has(3) && s->hud.ammo==60);
    /* Partial damage survives a room revisit, but the live patient respawns. */
    s=isolated();p=object(s,2);enemy=object(s,20);
    enemy->position[0]=0;enemy->position[2]=-10;shot(s);assert(enemy->solid);
    collect_key(s);p->position[0]=0;p->position[2]=-212;
    press(s,64UL);press(s,64UL);assert(studio_scene_apply_change());
    p=object(s,2);psx_scene_update(s,0);p->position[2]=190;
    press(s,64UL);assert(studio_scene_apply_change() && s->scene_id==2);
    enemy=object(s,20);assert(enemy->solid && enemy->position[0]==-130 && enemy->position[2]==-145);
    for(i=1;i<4;i++) {object(s,20U+(unsigned int)i)->position[0]=1000;object(s,20U+(unsigned int)i)->position[2]=1000;}
    p=object(s,2);p->position[0]=0;p->position[2]=170;p->rotation[1]=0;
    enemy->position[0]=0;enemy->position[2]=-10;
    shot(s);assert(enemy->solid);shot(s);assert(!enemy->solid);
    /* Ammo billboards exist throughout the campaign and pharmacy masks export. */
    s=isolated();p=object(s,2);p->position[0]=110;p->position[2]=80;
    press(s,64UL);assert(!object(s,35)->alive && s->hud.ammo==60);press(s,64UL);
    s=studio_scene_load_id(6);assert(s->backdrop_count==1 && s->backdrops[0].depth==550);
    assert(s->backdrops[0].w>100 && s->backdrops[0].w<=256 && studio_texture_count==7);
    assert(object(s,14)->solid && (object(s,14)->render_flags&PSX_RENDER_HIDDEN));
    assert(object(s,35)->alive && object(s,35)->kind==PSX_OBJECT_BILLBOARD);
    p=object(s,2);for(i=0;i<4;i++) {object(s,20U+(unsigned int)i)->position[0]=1000;object(s,20U+(unsigned int)i)->position[2]=1000;}
    p->position[0]=0;p->position[2]=0;p->rotation[1]=2048;
    for(i=0;i<100;i++) psx_scene_update(s,4096UL);
    assert(p->position[2]<=68); /* Must go around the physical counter. */
    p->position[0]=-150;p->position[2]=0;
    for(i=0;i<100;i++) psx_scene_update(s,4096UL);
    assert(p->position[2]>170); /* Open side passage reaches the front. */
    return 0;
}
