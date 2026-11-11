#ifndef STUDIO_FPS_H
#define STUDIO_FPS_H

/* Integer frame-rate sampling against the video clock, not update-call count.
 * fps_tenths uses 10 = 1 FPS; ticks_per_second is 50 for the PAL renderer. */
typedef struct PsxFrameRate {
    unsigned long start_tick;
    unsigned long frames;
    unsigned int ticks_per_second;
    int fps_tenths;
} PsxFrameRate;

static void psx_fps_init(PsxFrameRate *rate,unsigned long now,unsigned int hz)
{
    rate->start_tick=now;rate->frames=0;
    rate->ticks_per_second=hz;rate->fps_tenths=-1;
}

static void psx_fps_sample(PsxFrameRate *rate,unsigned long now)
{
    unsigned long elapsed;
    rate->frames++;
    elapsed=now-rate->start_tick;
    if(elapsed>=rate->ticks_per_second) {
        rate->fps_tenths=(int)(rate->frames*rate->ticks_per_second*10UL/elapsed);
        rate->start_tick=now;rate->frames=0;
    }
}

#endif
