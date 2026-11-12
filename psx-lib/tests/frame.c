#include "../runtime/frame.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    PsxFrameQueue q;
    PsxObject objects[2],snapshot[2];
    PsxScene scene,frame;
    int slot;
    PsxObjectRotation *rotation;
    char message[600];
    psx_frame_init(&q);
    assert(q.front==1 && psx_frame_build_slot(&q)==0);
    q.queued=0;assert(psx_frame_build_slot(&q)<0);
    assert(psx_frame_submit(&q)==0 && q.gpu==0);
    assert(psx_frame_build_slot(&q)==1); /* CPU/GPU overlap, different packets. */
    q.queued=1;assert(psx_frame_submit(&q)<0 && psx_frame_build_slot(&q)<0);
    assert(q.ready<0); /* Busy GPU cannot be exposed by a VBlank. */
    psx_frame_gpu_done(&q);assert(q.ready==0 && q.front==1);
    psx_frame_presented(&q,2);
    assert(q.front==0 && q.gpu<0 && q.ready<0 && q.queued==1 && q.count==1);
    slot=psx_frame_submit(&q);assert(slot==1 && slot!=q.front);
    assert(psx_frame_build_slot(&q)==0);
    q.queued=0;psx_frame_gpu_done(&q);psx_frame_presented(&q,4);
    assert(q.front==1 && q.count==2 && q.tick==4);
    psx_frame_discard(&q);assert(q.front==1 && q.gpu<0 && q.queued<0 && q.ready<0);
    assert(psx_frame_build_slot(&q)==0);
    q.queued=0;assert(psx_frame_submit(&q)==0);psx_frame_gpu_done(&q);
    /* Scene loads cancel completed/queued old work before replacing textures. */
    psx_frame_discard(&q);assert(q.front==1 && q.ready<0 && q.gpu<0);
    q.count=0xffffffffUL;q.queued=0;assert(psx_frame_submit(&q)==0);
    psx_frame_gpu_done(&q);psx_frame_presented(&q,0);assert(q.count==0);
    memset(objects,0,sizeof(objects));memset(&scene,0,sizeof(scene));
    scene.objects=objects;scene.count=2;scene.camera[0]=40;
    psx_overlay_rect(&scene,10,20,30,40,90,100,110);
    objects[0].position[0]=10;objects[1].position[0]=20;
    rotation=psx_object_rotation(&scene,&objects[0]);assert(rotation);
    strcpy(message,"FRAME TEXT");psx_overlay_text(&scene,message);
    psx_frame_snapshot(&frame,snapshot,&scene);
    rotation->m[0]=123;
    assert(psx_object_rotation_find(&frame,&snapshot[0])[0]==4096);
    objects[0].position[0]=99;objects[1].position[0]=98;
    scene.camera[0]=70;scene.overlay.commands[0].r=20;
    assert(frame.objects==snapshot && frame.objects!=scene.objects);
    assert(snapshot[0].position[0]==10 && snapshot[1].position[0]==20);
    assert(frame.camera[0]==40 && frame.overlay.commands[0].r==90);
    strcpy(message,"LIVE MENU HAS CHANGED");
    assert(strcmp(frame.overlay.text,"FRAME TEXT")==0);
    memset(message,'A',sizeof(message));message[sizeof(message)-1]=0;
    psx_overlay_text(&scene,message);psx_frame_snapshot(&frame,snapshot,&scene);
    assert(strlen(frame.overlay.text)==PSX_OVERLAY_TEXT_SIZE-1);
    psx_overlay_clear(&scene);psx_frame_snapshot(&frame,snapshot,&scene);assert(!frame.overlay.text[0] && !frame.overlay.count);
    memset(&scene,0,sizeof(scene));
    psx_overlay_rect(&scene,0,0,2,2,1,2,3);
    psx_overlay_text(&scene,"EMPTY SCENE");
    psx_frame_snapshot(&frame,snapshot,&scene);
    psx_overlay_clear(&scene);
    assert(frame.overlay.count==1 && !strcmp(frame.overlay.text,"EMPTY SCENE"));
    /* Shrinking and growing active arrays must never expose stale data. */
    scene.rotation_count=PSX_OBJECT_ROTATION_CAPACITY;
    scene.rotations[PSX_OBJECT_ROTATION_CAPACITY-1].m[8]=1234;
    scene.backdrop_count=PSX_MAX_BACKDROPS;scene.backdrops[PSX_MAX_BACKDROPS-1].depth=987;
    while(scene.overlay.count<PSX_OVERLAY_CAPACITY) assert(psx_overlay_rect(&scene,1,2,3,4,5,6,7));
    psx_frame_snapshot(&frame,snapshot,&scene);
    assert(frame.rotation_count==PSX_OBJECT_ROTATION_CAPACITY);
    assert(frame.rotations[PSX_OBJECT_ROTATION_CAPACITY-1].m[8]==1234);
    assert(frame.backdrops[PSX_MAX_BACKDROPS-1].depth==987);
    assert(frame.overlay.commands[PSX_OVERLAY_CAPACITY-1].r==5);
    scene.rotation_count=1;scene.backdrop_count=0;psx_overlay_clear(&scene);
    psx_frame_snapshot(&frame,snapshot,&scene);
    assert(frame.rotation_count==1 && !frame.backdrop_count && !frame.overlay.count);
    scene.rotation_count=PSX_OBJECT_ROTATION_CAPACITY;
    scene.rotations[PSX_OBJECT_ROTATION_CAPACITY-1].m[8]=4321;
    psx_frame_snapshot(&frame,snapshot,&scene);
    assert(frame.rotations[PSX_OBJECT_ROTATION_CAPACITY-1].m[8]==4321);
    puts("C89 frame queue: GPU ownership, overlap, VBlank, discard, wrap and frozen view passed.");
    return 0;
}
