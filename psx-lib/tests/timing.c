#include "../runtime/timing.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    PsxStepClock clock;
    unsigned int i,total;
    unsigned long now;
    psx_step_clock_reset(&clock,0);
    assert(psx_step_clock_steps(&clock,0)==0);
    total=0;
    for(i=1;i<=50;i++) total+=psx_step_clock_steps(&clock,i);
    assert(total==50);
    psx_step_clock_reset(&clock,0);total=0;
    for(i=2;i<=50;i+=2) total+=psx_step_clock_steps(&clock,i);
    assert(total==50);
    /* Irregular rendering, including iterations without a new video tick. */
    psx_step_clock_reset(&clock,0);total=0;now=0;
    for(i=0;i<10;i++) {
        total+=psx_step_clock_steps(&clock,now);
        now+=3;total+=psx_step_clock_steps(&clock,now);
        now+=2;total+=psx_step_clock_steps(&clock,now);
    }
    assert(now==50 && total==50);
    /* A long load/debugger stop must not cause an unbounded update backlog. */
    assert(psx_step_clock_steps(&clock,1000)==PSX_MAX_CATCHUP_STEPS);
    assert(psx_step_clock_steps(&clock,1000)==0);
    assert(psx_step_clock_steps(&clock,1001)==1);
    psx_step_clock_reset(&clock,2000);
    assert(psx_step_clock_steps(&clock,2000)==0);
    assert(psx_step_clock_steps(&clock,2001)==1);
    psx_step_clock_reset(&clock,0xfffffffeUL);
    assert(psx_step_clock_steps(&clock,1)==3);
    assert(psx_step_clock_steps(&clock,2)==1);
    puts("C89 timing: 50/25 FPS, irregular frames, stalls, load reset and 32-bit wrap passed.");
    return 0;
}
