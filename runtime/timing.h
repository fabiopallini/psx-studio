#ifndef STUDIO_TIMING_H
#define STUDIO_TIMING_H

/* One simulation step per PAL video tick: 50 steps/second, integer time only.
 * Reading elapsed ticks never waits. Rendering may span multiple video ticks.
 * Bound catch-up to 100 ms; discard older time after exceptional stalls rather
 * than spending indefinitely on updates. Reset after blocking Scene loads.
 * Mask subtraction to the console's 32-bit counter even on 64-bit host tests. */
#define PSX_SIMULATION_HZ 50
#define PSX_MAX_CATCHUP_STEPS 5

typedef struct PsxStepClock {
    unsigned long tick;
} PsxStepClock;

static void psx_step_clock_reset(PsxStepClock *clock,unsigned long now)
{
    clock->tick=now;
}

static unsigned int psx_step_clock_steps(PsxStepClock *clock,unsigned long now)
{
    unsigned long elapsed;
    elapsed=(now-clock->tick)&0xffffffffUL;
    clock->tick=now;
    if(elapsed>PSX_MAX_CATCHUP_STEPS) return PSX_MAX_CATCHUP_STEPS;
    return (unsigned int)elapsed;
}

#endif
