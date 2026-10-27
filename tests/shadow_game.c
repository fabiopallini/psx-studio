#include "scene.h"
#include "timing.h"
#include <assert.h>
#include <string.h>

static int player_sounds,enemy_sounds;
static int record_sound(PsxScene *scene,const char *name,int volume,int pan)
{
    (void)scene;assert(!strcmp(name,"shot.vag"));assert(pan>=-4096 && pan<=4096);
    if(volume==15000) player_sounds++;else {assert(volume==11000);enemy_sounds++;}
    return 1;
}
static PsxObject *object(PsxScene *scene,unsigned int id)
{
    unsigned int i;
    for(i=0;i<scene->count;i++) if(scene->objects[i].id==id) return &scene->objects[i];
    assert(0);return 0;
}
static void reset(void) {assert(studio_scene_load_id(2));}
static void at(PsxObject *a,const PsxObject *b)
{
    a->position[0]=b->position[0];a->position[2]=b->position[2];
}
static void fire(PsxScene *scene)
{psx_scene_update(scene,128UL);psx_scene_update(scene,0);}
static void aim_fire(PsxScene *scene,unsigned long direction)
{psx_scene_update(scene,4UL|direction);fire(scene);}
static void analog(PsxScene *scene,int x,int z)
{scene->input.analog=1;scene->input.x=x;scene->input.z=z;}
static void remove_walls(PsxScene *scene)
{
    unsigned int i;
    for(i=0;i<scene->count;i++) if(scene->objects[i].id>=10 && scene->objects[i].id<=19) scene->objects[i].alive=0;
}
static int music_requests;
static int record_music(PsxScene *scene,const char *name,int volume)
{
    (void)scene;assert(!strcmp(name,"patrol.vag") && volume==12000);
    music_requests++;return 1;
}
static void visual_feedback(void)
{
    PsxScene *scene;
    PsxObject *player,*guard,*slice,*wall;
    long full_scale;
    int i,before;
    reset();scene=studio_scene_load_id(2);player=object(scene,2);guard=object(scene,20);
    remove_walls(scene);object(scene,21)->alive=0;object(scene,50)->alive=0;
    scene->play_sound=record_sound;scene->play_music=record_music;before=enemy_sounds;
    player->position[0]=guard->position[0];player->position[2]=guard->position[2]-100;
    psx_scene_update(scene,0);
    assert(enemy_sounds==before && music_requests==1); /* Behind the south-facing guard. */
    for(i=140;i<144;i++) {
        slice=object(scene,(unsigned int)i);
        assert(slice->alive && (slice->render_flags&PSX_RENDER_DECAL));
        assert(slice->position[0]==guard->position[0] && slice->position[2]==guard->position[2]);
        assert(slice->position[1]==1 && slice->scale[0]>3800);
    }
    assert(!object(scene,144)->alive);
    /* Player moves sideways into the rear hemisphere; there is no snap turn
     * toward hidden input. Patrol turning is bounded to 64 units / two steps. */
    psx_scene_update(scene,0);assert(guard->rotation[1]>=1984 && guard->rotation[1]<=2112);
    assert(enemy_sounds==before && music_requests==1);
    /* A blocking wall shortens the fan and the hidden guard has no fan. */
    slice=object(scene,141);full_scale=slice->scale[0];
    wall=object(scene,10);wall->alive=1;wall->position[0]=guard->position[0];wall->position[2]=guard->position[2]+50;
    wall->half_size[0]=200;wall->half_size[2]=1;
    for(i=0;i<5;i++) psx_scene_update(scene,0);
    assert(slice->scale[0]<full_scale/2);
    guard->alive=0;psx_scene_update(scene,0);assert(!slice->alive);
    player_sounds=enemy_sounds=0;
}
static void character_volume(const PsxMesh *mesh)
{
    long min_x,max_x,min_z,max_z;
    int i;
    /* The first ten triangles form the torso. Keep a readable side profile
     * without adding geometry or changing the gameplay collision volume. */
    assert(mesh->count>=30);
    min_x=max_x=mesh->vertices[0].p[0];min_z=max_z=mesh->vertices[0].p[2];
    for(i=1;i<30;i++) {
        if(mesh->vertices[i].p[0]<min_x) min_x=mesh->vertices[i].p[0];
        if(mesh->vertices[i].p[0]>max_x) max_x=mesh->vertices[i].p[0];
        if(mesh->vertices[i].p[2]<min_z) min_z=mesh->vertices[i].p[2];
        if(mesh->vertices[i].p[2]>max_z) max_z=mesh->vertices[i].p[2];
    }
    assert((max_z-min_z)*2>=max_x-min_x);
}
static void action_feedback(void)
{
    PsxScene *scene;
    PsxObject *player,*guard,*bullet;
    const PsxMesh *rest;
    const PsxVertex *shared_vertices;
    const unsigned char *shared_audio;
    PsxSound *bank;
    int i,before;
    reset();scene=studio_scene_load_id(2);player=object(scene,2);rest=player->mesh;
    character_volume(rest);character_volume(object(scene,20)->mesh);
    shared_vertices=rest->vertices;shared_audio=scene->sounds[0].data;bank=scene->sounds;
    assert(object(scene,20)->mesh->vertices==object(scene,21)->mesh->vertices);
    assert(object(scene,65)->mesh->vertices!=shared_vertices);
    assert(studio_scene_load_id(3)==scene);
    assert(object(scene,2)->mesh->vertices==shared_vertices && scene->sounds[0].data==shared_audio);
    assert(scene->sounds!=bank); /* Immutable bytes shared, mutable banks separate. */
    reset();player=object(scene,2);rest=player->mesh;
    object(scene,20)->alive=object(scene,21)->alive=0;
    psx_scene_update(scene,128UL);assert(player->mesh==object(scene,65)->mesh && scene->hud.ammo==36);
    psx_scene_update(scene,0);bullet=object(scene,100);
    assert(player->mesh==object(scene,66)->mesh && object(scene,80)->alive);
    assert(scene->hud.ammo==35 && bullet->alive && bullet->kind==PSX_OBJECT_MESH);
    assert(bullet->rotation[1]==0 && (bullet->render_flags&PSX_RENDER_EMISSIVE));
    assert(bullet->half_size[0]==3 && bullet->half_size[2]==3);
    assert(bullet->mesh->count==18 && bullet->position[1]==33);
    for(i=0;i<4;i++) psx_scene_update(scene,0);
    assert(player->mesh==object(scene,67)->mesh && !object(scene,80)->alive);
    for(i=0;i<5;i++) psx_scene_update(scene,0);
    assert(player->mesh==rest);
    psx_scene_update(scene,128UL);psx_scene_update(scene,0);assert(object(scene,80)->alive);
    /* Circle no longer changes weapons or interrupts pistol feedback. */
    psx_scene_update(scene,32UL);assert(object(scene,80)->alive);
    assert(!psx_scene_find(scene,60) && !psx_scene_find(scene,64));
    /* Guards share the same shooting feedback; firing in an oblique heading
     * rotates the actual 3D tracer, without camera-facing billboard rotation. */
    reset();player=object(scene,2);guard=object(scene,20);object(scene,21)->alive=0;remove_walls(scene);
    player->position[0]=guard->position[0]+70;player->position[2]=guard->position[2]+100;
    scene->play_sound=record_sound;before=enemy_sounds;psx_scene_update(scene,0);
    assert(enemy_sounds==before+1 && guard->mesh==object(scene,68)->mesh && object(scene,81)->alive);
    bullet=object(scene,100);assert(bullet->rotation[1]==guard->rotation[1] && bullet->rotation[1]!=2048);
    player_sounds=enemy_sounds=0;
}
static void running(void)
{
    PsxScene *scene;
    PsxObject *player,*wall;
    const PsxMesh *rest;
    int i,seen;
    reset();scene=studio_scene_load_id(2);player=object(scene,2);rest=player->mesh;
    remove_walls(scene);object(scene,20)->alive=object(scene,21)->alive=0;
    object(scene,50)->alive=0;
    psx_scene_update(scene,8192UL);assert(player->mesh==object(scene,70)->mesh);
    for(i=0;i<5;i++) psx_scene_update(scene,8192UL);
    assert(player->mesh==object(scene,76)->mesh);
    for(i=0;i<6;i++) psx_scene_update(scene,8192UL);
    assert(player->mesh==object(scene,71)->mesh);
    for(i=0;i<6;i++) psx_scene_update(scene,8192UL);
    assert(player->mesh==object(scene,76)->mesh);
    assert(player->mesh->count==rest->count && player->half_size[0]==10);
    psx_scene_update(scene,0);assert(player->mesh==rest);
    psx_scene_update(scene,128UL|8192UL);assert(player->mesh==object(scene,65)->mesh);
    psx_scene_update(scene,0);
    for(i=0;i<9;i++) psx_scene_update(scene,0);
    /* Holding a direction into cover must not run on the spot. */
    wall=object(scene,10);wall->alive=1;
    wall->position[0]=player->position[0]+12;wall->position[2]=player->position[2];
    wall->half_size[0]=1;wall->half_size[2]=100;
    for(i=0;i<12;i++) psx_scene_update(scene,8192UL);
    assert(player->mesh==rest);
    reset();player=object(scene,2);remove_walls(scene);
    object(scene,21)->alive=object(scene,50)->alive=0;
    player->position[0]=object(scene,20)->position[0];
    player->position[2]=object(scene,20)->position[2]-100;
    seen=0;
    for(i=0;i<80;i++) {
        psx_scene_update(scene,0);
        if(object(scene,20)->mesh==object(scene,74)->mesh) seen|=1;
        if(object(scene,20)->mesh==object(scene,75)->mesh) seen|=2;
        if(object(scene,20)->mesh==object(scene,78)->mesh) seen|=4;
    }
    assert(seen==7);
}
static void stealth(void)
{
    PsxScene *scene;
    PsxObject *player,*guard,*wall;
    int i,before;
    long x,z;
    reset();scene=studio_scene_load_id(2);player=object(scene,2);guard=object(scene,20);
    remove_walls(scene);
    object(scene,21)->alive=0;object(scene,50)->alive=0;
    scene->play_sound=record_sound;before=enemy_sounds;
    player->position[0]=guard->position[0];player->position[2]=guard->position[2]+100;
    psx_scene_update(scene,0);assert(enemy_sounds==before+1); /* Immediate sight. */
    for(i=0;i<44;i++) psx_scene_update(scene,0);
    assert(enemy_sounds==before+1);
    psx_scene_update(scene,0);assert(enemy_sounds==before+2);
    /* An arbitrarily thin wall blocks the exact segment; no sampled-ray gaps. */
    wall=object(scene,10);wall->alive=1;wall->position[0]=guard->position[0];
    wall->position[2]=guard->position[2]+43;wall->half_size[0]=100;wall->half_size[2]=1;
    before=enemy_sounds;
    for(i=0;i<100;i++) psx_scene_update(scene,0);
    assert(enemy_sounds==before);
    /* With no target, patrol remains local to the initial spawn on both axes. */
    reset();player=object(scene,2);guard=object(scene,20);x=guard->position[0];z=guard->position[2];
    remove_walls(scene);
    object(scene,21)->alive=0;object(scene,50)->alive=0;player->position[0]=1000;player->position[2]=1000;
    for(i=0;i<1200;i++) {
        psx_scene_update(scene,0);
        assert(guard->position[0]>=x-32 && guard->position[0]<=x+32);
        assert(guard->position[2]>=z-32 && guard->position[2]<=z+32);
    }
    assert(guard->position[0]!=x || guard->position[2]!=z);
    player_sounds=enemy_sounds=0;
}
static void pistol_hit_reaction(void)
{
    PsxScene *scene;
    PsxObject *player,*guard;
    int i;
    reset();scene=studio_scene_load_id(2);player=object(scene,2);guard=object(scene,20);
    remove_walls(scene);object(scene,21)->alive=object(scene,50)->alive=0;
    player->position[0]=guard->position[0];player->position[2]=guard->position[2]-100;
    player->rotation[1]=2048;scene->play_sound=record_sound;enemy_sounds=0;
    fire(scene);
    for(i=0;i<40;i++) psx_scene_update(scene,0);
    assert(guard->alive && enemy_sounds>0 && scene->hud.ammo==35);
    assert(guard->rotation[1]<64 || guard->rotation[1]>4032);
    /* A miss must not reveal an unseen player behind the guard. */
    reset();player=object(scene,2);guard=object(scene,20);remove_walls(scene);
    object(scene,21)->alive=object(scene,50)->alive=0;
    player->position[0]=guard->position[0];player->position[2]=guard->position[2]-100;
    scene->play_sound=record_sound;enemy_sounds=0;fire(scene);
    for(i=0;i<40;i++) psx_scene_update(scene,0);
    assert(guard->alive && !enemy_sounds);
    assert(guard->rotation[1]>=2048 && guard->rotation[1]<=3072);
    player_sounds=enemy_sounds=0;
}
static void controls(void)
{
    PsxScene *scene;
    PsxObject *player;
    long x,z,angle;
    int i;
    scene=studio_scene_load_id(2);player=object(scene,2);scene->play_sound=record_sound;
    object(scene,20)->alive=0;object(scene,21)->alive=0;
    x=player->position[0];z=player->position[2];
    for(i=0;i<50;i++) psx_scene_update(scene,128UL);
    assert(scene->hud.ammo==36 && !player_sounds && player->position[0]==x && player->position[2]==z);
    for(i=0;i<128;i++) psx_scene_update(scene,128UL|32768UL);
    assert(player->rotation[1]==0 && player->position[0]==x && player->position[2]==z); /* Full circle. */
    for(i=0;i<7;i++) psx_scene_update(scene,128UL|8192UL);
    angle=player->rotation[1];assert(angle==3872 && angle%512!=0);
    psx_scene_update(scene,0);assert(scene->hud.ammo==35 && player_sounds==1);
    for(i=0;i<3;i++) fire(scene);
    assert(scene->hud.ammo==32 && player_sounds==4);
    scene->hud.ammo=0;fire(scene);assert(player_sounds==4);
    reset();object(scene,20)->alive=0;object(scene,21)->alive=0;scene->play_sound=record_sound;player=object(scene,2);
    object(scene,20)->alive=1;object(scene,20)->position[0]=player->position[0];object(scene,20)->position[2]=player->position[2]-80;
    object(scene,21)->alive=0;
    for(i=0;i<200 && !enemy_sounds;i++) psx_scene_update(scene,0);
    assert(enemy_sounds>0);
    /* Cross has no movement ability; held directions keep ordinary walking. */
    reset();object(scene,20)->alive=0;object(scene,21)->alive=0;player=object(scene,2);x=player->position[0];z=player->position[2];
    psx_scene_update(scene,64UL);assert(player->position[0]==x && player->position[2]==z);
    psx_scene_update(scene,64UL|8192UL);assert(player->position[0]==x+2);
    psx_scene_update(scene,0);assert(player->position[0]==x+2);
    assert(!psx_scene_find(scene,120));
    /* Arbitrary analog aim and shot direction. */
    reset();object(scene,20)->alive=0;object(scene,21)->alive=0;player=object(scene,2);x=player->position[0];z=player->position[2];
    analog(scene,100,-45);psx_scene_update(scene,128UL);angle=player->rotation[1];
    assert(angle>3300 && angle<3400 && angle%512!=0 && player->position[0]==x && player->position[2]==z);
    analog(scene,0,0);psx_scene_update(scene,0);
    assert(player->rotation[1]==angle && scene->hud.ammo==35 && object(scene,100)->position[0]>x && object(scene,100)->position[2]<z);
    /* Cross leaves analog walking and digital input precedence unchanged. */
    reset();object(scene,20)->alive=0;object(scene,21)->alive=0;player=object(scene,2);x=player->position[0];analog(scene,-127,0);
    psx_scene_update(scene,64UL);assert(player->position[0]==x-2);
    psx_scene_update(scene,64UL|8192UL);assert(player->position[0]==x);
    /* A save dialog cancels an armed release, even when it closes with Cross. */
    reset();object(scene,20)->alive=0;object(scene,21)->alive=0;player=object(scene,2);psx_scene_update(scene,128UL);at(player,object(scene,50));
    psx_scene_update(scene,128UL);psx_scene_update(scene,0);psx_scene_update(scene,64UL);psx_scene_update(scene,0);
    assert(scene->hud.ammo==36);
}
static void cancel_aim(void)
{
    PsxScene *scene;
    PsxObject *player;
    const PsxMesh *rest;
    long x;
    int i;
    scene=studio_scene_load_id(2);player=object(scene,2);rest=player->mesh;
    object(scene,20)->alive=object(scene,21)->alive=object(scene,50)->alive=0;
    scene->play_sound=record_sound;player_sounds=0;
    psx_scene_update(scene,128UL);assert(player->mesh==object(scene,65)->mesh);
    psx_scene_update(scene,128UL|64UL);assert(player->mesh==rest);
    for(i=0;i<3;i++) psx_scene_update(scene,128UL);
    assert(player->mesh==rest && scene->hud.ammo==36 && !player_sounds);
    x=player->position[0];psx_scene_update(scene,128UL|8192UL);
    assert(player->position[0]==x+2 && player->rotation[1]==3072);
    psx_scene_update(scene,0);assert(scene->hud.ammo==36 && !player_sounds);
    assert(!object(scene,100)->alive && !object(scene,80)->alive);
    fire(scene);assert(scene->hud.ammo==35 && player_sounds==1);
    /* Cancel wins on the release step and on a simultaneous initial press. */
    psx_scene_update(scene,128UL);psx_scene_update(scene,64UL);
    assert(scene->hud.ammo==35 && player_sounds==1);
    psx_scene_update(scene,0);psx_scene_update(scene,128UL|64UL);
    psx_scene_update(scene,128UL);psx_scene_update(scene,0);
    assert(scene->hud.ammo==35 && player_sounds==1);
    fire(scene);assert(scene->hud.ammo==34 && player_sounds==2);
    player_sounds=0;
}
static void secured_passages(void)
{
    PsxScene *scene;
    PsxObject *player,*door;
    long closed;
    int i;
    scene=studio_scene_load_id(2);player=object(scene,2);door=object(scene,42);closed=door->position[1];
    assert(door->alive && door->solid && !object(scene,40)->alive);
    player->position[0]=220;player->position[2]=-200;
    for(i=0;i<40;i++) psx_scene_update(scene,4096UL);
    assert(player->position[2]>=-236 && !scene->pending_scene);
    object(scene,20)->alive=0;psx_scene_update(scene,0);assert(door->solid && door->position[1]==closed);
    object(scene,21)->alive=0;
    for(i=0;i<18;i++) psx_scene_update(scene,0);
    assert(!door->solid && door->alive && door->position[1]==closed+72 && !scene->pending_scene);
    for(i=0;i<40 && !scene->pending_scene;i++) psx_scene_update(scene,4096UL);
    assert(scene->pending_scene==3 && player->position[2]<-270);assert(studio_scene_apply_change());
    assert(object(scene,42)->solid && object(scene,44)->solid);
    player=object(scene,2);object(scene,50)->alive=0;player->position[0]=-220;player->position[2]=200;
    object(scene,20)->alive=object(scene,21)->alive=0;
    for(i=0;i<18;i++) psx_scene_update(scene,0);
    for(i=0;i<45 && !scene->pending_scene;i++) psx_scene_update(scene,16384UL);
    assert(scene->pending_scene==2 && player->position[2]>270);assert(studio_scene_apply_change());
    assert(object(scene,42)->solid && object(scene,42)->position[1]==closed);
}
static void movement_speed(void)
{
    static const unsigned long directions[8]={4096UL,4096UL|8192UL,8192UL,
        8192UL|16384UL,16384UL,16384UL|32768UL,32768UL,32768UL|4096UL};
    PsxScene *scene;
    PsxObject *player;
    long x,z,distance;
    int mode,d,step;
    /* One second in open space: both controller types travel about 100 units
     * in every direction, with less than two units of integer rounding. */
    for(mode=0;mode<2;mode++) for(d=0;d<8;d++) {
        scene=studio_scene_load_id(2);player=object(scene,2);remove_walls(scene);
        object(scene,20)->alive=object(scene,21)->alive=object(scene,50)->alive=0;
        player->position[0]=player->position[2]=1000;
        if(mode) analog(scene,(directions[d]&8192UL)?127:(directions[d]&32768UL)?-127:0,
                            (directions[d]&16384UL)?127:(directions[d]&4096UL)?-127:0);
        for(step=0;step<50;step++) psx_scene_update(scene,mode?0:directions[d]);
        x=player->position[0]-1000;z=player->position[2]-1000;distance=x*x+z*z;
        assert(distance>=9604 && distance<=10000);
        psx_scene_update(scene,128UL);assert(player->position[0]==1000+x && player->position[2]==1000+z);
    }
}
static void timed_game(void)
{
    PsxScene *scene;
    PsxStepClock clock;
    PsxObject reference[72];
    unsigned long now,delta;
    long initial_x;
    unsigned int mode,frame,steps,step,i,axis,updates;
    const unsigned int irregular[5]={0,3,1,2,4};
    int health,ammo;
    health=ammo=0;
    /* Two real seconds at 50 FPS, 25 FPS and uneven render intervals must
     * produce identical movement, firing cooldowns, bullets and guard state. */
    for(mode=0;mode<3;mode++) {
        scene=studio_scene_load_id(2);assert(scene);initial_x=object(scene,2)->position[0];
        assert(scene->count<=72);psx_step_clock_reset(&clock,0);now=0;frame=0;updates=0;
        while(now<100) {
            delta=mode==0?1:mode==1?2:irregular[frame%5];frame++;
            if(now+delta>100) delta=100-now;
            now+=delta;steps=psx_step_clock_steps(&clock,now);
            for(step=0;step<steps;step++) {psx_scene_update(scene,updates<50?8192UL:updates&1?128UL:0);updates++;}
            assert(!scene->pending_scene);
        }
        assert(object(scene,2)->position[0]==initial_x+100 && scene->hud.ammo<36);
        if(!mode) {
            memcpy(reference,scene->objects,scene->count*sizeof(PsxObject));
            health=scene->hud.health;ammo=scene->hud.ammo;
        } else {
            assert(scene->hud.health==health && scene->hud.ammo==ammo);
            for(i=0;i<scene->count;i++) {
                assert(scene->objects[i].alive==reference[i].alive);
                assert(scene->objects[i].mesh==reference[i].mesh);
                for(axis=0;axis<3;axis++) {
                    assert(scene->objects[i].position[axis]==reference[i].position[axis]);
                    assert(scene->objects[i].rotation[axis]==reference[i].rotation[axis]);
                }
            }
        }
    }
}
static void grenade_combat(void)
{
    PsxScene *scene;
    PsxObject *player,*guard,*n,*wall;
    int i,mode,health,found;
    long x,z;
    /* Direct hit, evasion and a wall inserted between landing and player.
     * Disable bullet visuals so blast damage is observed in isolation. */
    for(mode=0;mode<4;mode++) {
        scene=studio_scene_load_id(4);player=object(scene,2);guard=object(scene,20);
        remove_walls(scene);object(scene,21)->alive=0;
        player->position[0]=0;player->position[2]=0;
        guard->position[0]=0;guard->position[2]=-140;guard->rotation[1]=2048;
        for(i=100;i<112;i++) object(scene,(unsigned int)i)->half_size[0]=0;
        found=0;
        for(i=0;i<90;i++) {
            psx_scene_update(scene,0);
            if(object(scene,160)->alive) {found=1;break;}
        }
        assert(found && guard->mesh==object(scene,79)->mesh && object(scene,164)->alive);
        n=object(scene,160);x=player->position[0];z=player->position[2];
        guard->alive=0;
        /* Drain the already-fired bullets, then restore health and wait for impact. */
        if(mode==1) player->position[0]=100;
        if(mode==2) {
            player->position[0]=40;
            wall=object(scene,10);wall->alive=wall->solid=1;
            wall->position[0]=20;wall->position[2]=0;
            wall->half_size[0]=2;wall->half_size[2]=80;
        }
        if(mode==3) {
            wall=object(scene,10);wall->alive=wall->solid=1;
            wall->position[0]=0;wall->position[2]=-70;
            wall->half_size[0]=80;wall->half_size[2]=2;
        }
        health=0;
        for(i=0;i<45;i++) {
            scene->hud.health=100;
            psx_scene_update(scene,0);
            if(!n->alive) {health=scene->hud.health;break;}
        }
        assert(i<45 && object(scene,162)->alive && n->position[1]<=3 && !object(scene,164)->alive);
        assert(n->position[0]>=x-2 && n->position[0]<=x+2);
        if(mode==3) assert(n->position[2]<-70);
        else assert(n->position[2]>=z-8 && n->position[2]<=z+8);
        if(mode) assert(health==100);else assert(health==65);
        for(i=0;i<12;i++) psx_scene_update(scene,0);
        assert(!object(scene,162)->alive);
    }
    /* Rendering at 50 Hz or in uneven batches must produce identical combat. */
    {
        PsxObject reference[96];
        unsigned int count,j,k,step,total,batch;
        static const unsigned int cadence[4]={1,3,2,4};
        int reference_health;
        reference_health=0;count=0;
        for(mode=0;mode<2;mode++) {
            scene=studio_scene_load_id(4);remove_walls(scene);player=object(scene,2);
            player->position[0]=0;player->position[2]=0;
            guard=object(scene,20);guard->position[0]=0;guard->position[2]=-140;guard->rotation[1]=2048;
            object(scene,21)->alive=0;total=0;j=0;
            while(total<130) {
                batch=mode?cadence[j++%4]:1;
                if(total+batch>130) batch=130-total;
                for(step=0;step<batch;step++) {psx_scene_update(scene,0);total++;}
            }
            if(!mode) {
                count=scene->count;assert(count<=96);
                memcpy(reference,scene->objects,count*sizeof(PsxObject));reference_health=scene->hud.health;
            } else {
                assert(scene->count==count && scene->hud.health==reference_health);
                for(j=0;j<count;j++) {
                    assert(scene->objects[j].alive==reference[j].alive && scene->objects[j].mesh==reference[j].mesh);
                    for(k=0;k<3;k++) assert(scene->objects[j].position[k]==reference[j].position[k]);
                }
            }
        }
    }
    /* Scene reload resets active flight/effects; prior sectors have no grenades. */
    scene=studio_scene_load_id(4);assert(!object(scene,160)->alive && !object(scene,162)->alive);
}
int main(void)
{
    PsxScene *scene;
    PsxObject *player,*shot;
    int i,seen;
    long z;
    const unsigned long aim_keys[8]={4096UL,4096UL|8192UL,8192UL,8192UL|16384UL,
        16384UL,16384UL|32768UL,32768UL,32768UL|4096UL};
    const long aim_angles[8]={0,3584,3072,2560,2048,1536,1024,512};
    visual_feedback();action_feedback();running();stealth();pistol_hit_reaction();controls();
    scene=studio_scene_load();assert(!scene->sound_count && !scene->play_sound);

    assert(scene->scene_id==1 && scene->hud.style==PSX_HUD_MENU);
    psx_scene_update(scene,16384UL);psx_scene_update(scene,0);psx_scene_update(scene,64UL);
    assert(scene->checkpoint.operation==PSX_CHECKPOINT_LOAD);
    scene->checkpoint.operation=0;scene->checkpoint.status=-1;psx_scene_update(scene,0);
    assert(strstr(scene->hud.message,"NO VALID CHECKPOINT"));
    psx_scene_update(scene,64UL);psx_scene_update(scene,0);psx_scene_update(scene,4096UL);
    psx_scene_update(scene,0);psx_scene_update(scene,64UL);
    assert(scene->pending_scene==2 && !scene->checkpoint.operation);
    assert(studio_scene_apply_change());player=object(scene,2);
    assert(scene->scene_id==2 && scene->hud.enabled && scene->hud.health==100 && scene->hud.ammo==36);
    assert(object(scene,1)->render_flags==PSX_RENDER_BACKGROUND && player->render_flags==PSX_RENDER_CULL_BACKFACES);
    assert(object(scene,10)->render_flags==PSX_RENDER_CULL_BACKFACES);
    assert(scene->projection==1 && object(scene,200)->follow_target==2);
    for(i=100;i<112;i++) assert(!object(scene,(unsigned int)i)->alive);
    assert(!object(scene,40)->alive);
    assert(scene->hud.style==PSX_HUD_COMBAT && !scene->checkpoint.operation);
    reset();player=object(scene,2);at(player,object(scene,50));
    psx_scene_update(scene,64UL);assert(scene->hud.style==PSX_HUD_MENU && !scene->checkpoint.operation);
    psx_scene_update(scene,0);psx_scene_update(scene,64UL); /* Default No. */
    assert(scene->hud.style==PSX_HUD_COMBAT && !scene->checkpoint.operation);
    player->position[0]+=50;psx_scene_update(scene,0);at(player,object(scene,50));
    psx_scene_update(scene,0);psx_scene_update(scene,4096UL);psx_scene_update(scene,0);psx_scene_update(scene,64UL);
    assert(scene->checkpoint.operation==PSX_CHECKPOINT_SAVE && scene->checkpoint.scene_id==1);
    scene->checkpoint.operation=0;scene->checkpoint.status=1;psx_scene_update(scene,0);
    assert(strstr(scene->hud.message,"CHECKPOINT SAVED"));psx_scene_update(scene,64UL);
    assert(scene->hud.style==PSX_HUD_COMBAT);
    reset();player=object(scene,2);object(scene,20)->alive=0;object(scene,21)->alive=0;
    fire(scene);shot=object(scene,100);
    assert(scene->hud.ammo==35 && shot->alive);z=shot->position[2];
    psx_scene_update(scene,0);assert(shot->position[2]<z);
    z=player->position[2];psx_scene_update(scene,4UL|8192UL);
    assert(player->rotation[1]==3072 && player->position[2]==z);
    psx_scene_update(scene,8UL|4096UL);
    assert(player->rotation[1]==3072 && player->position[2]<z);
    psx_scene_update(scene,4UL|4096UL|8192UL);
    assert(player->rotation[1]==3584);
    for(i=0;i<8;i++) {psx_scene_update(scene,4UL|aim_keys[i]);assert(player->rotation[1]==aim_angles[i]);}
    reset();player=object(scene,2);player->position[0]=-125;player->position[2]=20;
    for(i=0;i<20;i++) psx_scene_update(scene,8192UL);
    assert(player->position[0]<=-118);
    reset();player=object(scene,2);player->position[0]=-160;player->position[2]=20;
    aim_fire(scene,8192UL);
    for(i=0;i<20;i++) psx_scene_update(scene,0);
    assert(!object(scene,100)->alive); /* Bullet stopped by solid cover. */
    reset();player=object(scene,2);psx_object_kill(object(scene,21));
    object(scene,20)->position[0]=player->position[0]+14;object(scene,20)->position[2]=player->position[2]-40;
    for(i=0;i<25;i++) fire(scene);
    assert(!object(scene,20)->alive); /* Collider + projectile radius catches the graze. */
    reset();player=object(scene,2);psx_object_kill(object(scene,21));
    object(scene,20)->position[0]=player->position[0];object(scene,20)->position[2]=player->position[2]-5;
    for(i=0;i<15;i++) fire(scene);
    assert(!object(scene,20)->alive); /* Initial muzzle segment cannot skip close guards. */
    reset();player=object(scene,2);at(player,object(scene,32));psx_scene_update(scene,0);
    assert(scene->hud.ammo==54 && !object(scene,32)->alive);
    scene->hud.ammo=98;at(player,object(scene,33));psx_scene_update(scene,0);
    assert(scene->hud.ammo==99);
    scene->hud.health=40;at(player,object(scene,30));psx_scene_update(scene,0);
    assert(scene->hud.health==75 && !object(scene,30)->alive);
    scene->hud.health=95;at(player,object(scene,31));psx_scene_update(scene,0);
    assert(scene->hud.health==100);
    reset();player=object(scene,2);player->position[0]=-60;player->position[2]=150;
    object(scene,20)->position[0]=-60;object(scene,20)->position[2]=-80;psx_object_kill(object(scene,21));
    for(i=0;i<300;i++) psx_scene_update(scene,0);
    assert(scene->hud.health==100); /* Cover blocks hostile line of sight. */
    reset();player=object(scene,2);object(scene,20)->position[0]=object(scene,2)->position[0];object(scene,20)->position[2]=30;
    psx_object_kill(object(scene,21));seen=0;
    for(i=0;i<300;i++) {
        psx_scene_update(scene,0);
        if(object(scene,100)->alive) seen=1;
    }
    assert(seen && scene->hud.health<100 && scene->hud.health>0);
    reset();object(scene,20)->position[0]=object(scene,2)->position[0];object(scene,20)->position[2]=30;
    psx_object_kill(object(scene,21));
    for(i=0;i<100;i++) fire(scene);
    assert(!object(scene,20)->alive); /* Two player hits kill a guard. */
    for(i=0;i<18;i++) psx_scene_update(scene,0);
    player=object(scene,2);at(player,object(scene,40));psx_scene_update(scene,0);
    assert(scene->pending_scene==3);assert(studio_scene_apply_change());
    assert(scene->scene_id==3 && scene->hud.health==100 && scene->hud.ammo==36);
    player=object(scene,2);at(player,object(scene,41));psx_scene_update(scene,0);
    assert(!object(scene,40)->alive && !object(scene,41)->alive && !scene->pending_scene);
    psx_object_kill(object(scene,20));psx_scene_update(scene,0);
    assert(!object(scene,40)->alive && !object(scene,41)->alive);
    psx_object_kill(object(scene,20));psx_object_kill(object(scene,21));psx_scene_update(scene,0);
    for(i=0;i<18;i++) psx_scene_update(scene,0);
    assert(scene->pending_scene==4);assert(studio_scene_apply_change());
    assert(scene->scene_id==4 && object(scene,160) && object(scene,79));
    object(scene,20)->alive=object(scene,21)->alive=0;
    for(i=0;i<18;i++) psx_scene_update(scene,0);
    at(object(scene,2),object(scene,41));psx_scene_update(scene,0);
    assert(scene->pending_scene==5);assert(studio_scene_apply_change());
    object(scene,20)->alive=object(scene,21)->alive=0;
    for(i=0;i<18;i++) psx_scene_update(scene,0);
    at(object(scene,2),object(scene,41));psx_scene_update(scene,0);
    assert(strstr(scene->hud.message,"MISSION COMPLETE") && scene->hud.style==PSX_HUD_MENU);
    psx_scene_update(scene,2048UL);assert(scene->pending_scene==5);assert(studio_scene_apply_change());
    assert(scene->hud.health==100 && object(scene,20)->alive && object(scene,30)->alive);
    scene->hud.health=0;psx_scene_update(scene,0);assert(strstr(scene->hud.message,"YOU ARE DOWN") && scene->hud.style==PSX_HUD_MENU);
    psx_scene_update(scene,2048UL);assert(studio_scene_apply_change());assert(scene->hud.health==100);
    cancel_aim();secured_passages();movement_speed();timed_game();grenade_combat();
    /* Existing card sector codes retain their meaning after menu renumbering. */
    for(i=1;i<=2;i++) {
        scene=studio_scene_load_id(1);assert(scene && scene->hud.style==PSX_HUD_MENU);
        psx_scene_update(scene,16384UL);psx_scene_update(scene,0);psx_scene_update(scene,64UL);
        assert(scene->checkpoint.operation==PSX_CHECKPOINT_LOAD);
        scene->checkpoint.operation=0;scene->checkpoint.status=1;scene->checkpoint.scene_id=(unsigned int)i;
        psx_scene_update(scene,0);assert(scene->pending_scene==(unsigned int)(i+1));assert(studio_scene_apply_change());
        assert(scene->scene_id==(unsigned int)(i+1) && scene->hud.style==PSX_HUD_COMBAT && !scene->checkpoint.operation);
    }
    player=object(scene,2);at(player,object(scene,50));psx_scene_update(scene,0);
    psx_scene_update(scene,4096UL);psx_scene_update(scene,0);psx_scene_update(scene,64UL);
    assert(scene->checkpoint.operation==PSX_CHECKPOINT_SAVE && scene->checkpoint.scene_id==2);
    return 0;
}
