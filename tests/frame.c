#include "../runtime/frame.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    PsxFrameQueue q;
    PsxObject objects[2],snapshot[2];
    PsxScene scene,frame;
    int slot;
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
    scene.objects=objects;scene.count=2;scene.camera[0]=40;scene.hud.health=100;
    objects[0].position[0]=10;objects[1].position[0]=20;
    psx_frame_snapshot(&frame,snapshot,&scene);
    objects[0].position[0]=99;objects[1].position[0]=98;
    scene.camera[0]=70;scene.hud.health=50;
    assert(frame.objects==snapshot && frame.objects!=scene.objects);
    assert(snapshot[0].position[0]==10 && snapshot[1].position[0]==20);
    assert(frame.camera[0]==40 && frame.hud.health==100);
    puts("C89 frame queue: GPU ownership, overlap, VBlank, discard, wrap and frozen view passed.");
    return 0;
}
