#include "../runtime/fps.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    PsxFrameRate rate;
    unsigned long i,start;
    psx_fps_init(&rate,100,50);assert(rate.fps_tenths==-1);
    for(i=1;i<=49;i++) psx_fps_sample(&rate,100+i);
    assert(rate.fps_tenths==-1);
    psx_fps_sample(&rate,150);assert(rate.fps_tenths==500);
    for(i=1;i<=25;i++) psx_fps_sample(&rate,150+i*2);
    assert(rate.fps_tenths==250);
    psx_fps_init(&rate,0,50);
    for(i=1;i<=17;i++) psx_fps_sample(&rate,i*3);
    assert(rate.fps_tenths==166); /* 17 frames / 51 PAL ticks, not 17 FPS. */
    psx_fps_init(&rate,0,50);psx_fps_sample(&rate,100);
    assert(rate.fps_tenths==5); /* A long stall counts elapsed video ticks. */
    start=~0UL-24;psx_fps_init(&rate,start,50);
    for(i=1;i<=50;i++) psx_fps_sample(&rate,start+i);
    assert(rate.fps_tenths==500);
    psx_fps_init(&rate,1000,50);assert(rate.fps_tenths==-1);
    puts("C89 FPS: sampling, missed vblanks, stalls, reset and counter wrap passed.");
    return 0;
}
