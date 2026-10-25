#include "../runtime/timing.h"
#include "console/scene.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* Run the actual generated main: input/update calls occur while rendering is
 * unavailable, and a request during a render yield must discard that frame. */
#define main studio_game_main
#include "../pipeline/project-main.c"
#undef main

unsigned long studio_pad;
PsxTexture studio_textures[1];
unsigned int studio_texture_count;
const char *studio_scene_name="Loop test";
static PsxObject actor,frame_actor;
static PsxScene scene,frame_scene;
static PsxStepClock clock;
static jmp_buf done;
static unsigned long now;
static int turn,loads,updates,first_updates,draws,mode,input_wait,rendering,loading_screens;
static const unsigned long times[10]={0,1,2,3,4,5,206,207,207,208};
static const unsigned long inputs[10]={0,0,32768,8192,8192,8192,8192,0,8192,8192};

void psInit(void)
{
    memset(&actor,0,sizeof(actor));memset(&scene,0,sizeof(scene));
    scene.objects=&actor;scene.count=1;scene.scene_id=1;
    loading_screens=0;turn=-1;now=0;loads=updates=first_updates=draws=input_wait=rendering=0;
}
void psShowLoading(const char *message)
{
    assert(message && *message);loading_screens++;
    assert(loading_screens==1 || scene.pending_scene==2);
}
PsxScene *studio_scene_load(void) {assert(loading_screens==1);return &scene;}
int psSetScene(PsxScene *s,PsxTexture *textures,unsigned int count)
{
    (void)textures;(void)count;assert(s==&scene);loads++;
    if(loads==2) {assert(turn==5);now+=200;input_wait=1;}
    psx_step_clock_reset(&clock,now);return 1;
}
int psUpdate(void)
{
    unsigned int i,steps;
    unsigned long input;
    if(rendering) {now=1;studio_pad=8192;}
    else {turn++;assert(turn<10);now=times[turn];studio_pad=inputs[turn];}
    if(scene.pending_scene) return 0;
    steps=psx_step_clock_steps(&clock,now);
    for(i=0;i<steps;i++) {
        input=studio_pad;
        if(input_wait) {if(!input) input_wait=0;input=0;}
        psx_scene_update(&scene,input);
        if(scene.pending_scene) break;
    }
    if(!mode && turn==2) assert(draws==1 && actor.position[0]==0);
    return steps!=0;
}
void psx_scene_update(PsxScene *s,unsigned long input)
{
    assert(s==&scene);updates++;
    if(input&8192UL) actor.position[0]+=2;
    if(input&32768UL) actor.position[0]-=2;
    if(s->scene_id==1) {
        first_updates++;
        if(first_updates==(mode?1:3)) s->pending_scene=2;
    } else if(turn==6 || turn==7) assert(input==0);
}
int psGpuIdle(void) {assert(scene.pending_scene);return turn>=5;}
int studio_scene_apply_change(void)
{
    assert(turn==5 && scene.pending_scene==2 && loading_screens==2);
    scene.scene_id=2;scene.pending_scene=0;actor.position[0]=0;return 1;
}
int psBeginFrame(void)
{
    assert(!scene.pending_scene);
    if(turn!=0 && turn!=6 && turn!=9) return 0;
    frame_actor=actor;frame_scene=scene;frame_scene.objects=&frame_actor;return 1;
}
const PsxScene *psDrawScene(void)
{
    if(turn==0) {
        rendering=1;psUpdate();rendering=0;
        assert(frame_actor.position[0]==0 && actor.position[0]==2);
    }
    return &frame_scene;
}
void psDrawHud(void) {}
int psFontEnabled(void) {return 0;}
int psFrameRate(void) {return -1;}
int FntPrint(const char *format,...) {(void)format;return 0;}
void psDisplay(void)
{
    assert(!scene.pending_scene);draws++;
    if(turn==9) {
        assert(loads==2 && scene.scene_id==2 && actor.position[0]==2);
        assert(first_updates==(mode?1:3) && updates==(mode?4:6));
        assert(draws==(mode?2:3));
        longjmp(done,1);
    }
}
int main(void)
{
    for(mode=0;mode<2;mode++) if(!setjmp(done)) {studio_game_main();assert(0);}
    puts("C89 game loop: input during GPU waits, render yields, safe Scene load and frame cancellation passed.");
    return 0;
}
