#ifndef STUDIO_FRAME_H
#define STUDIO_FRAME_H
#include "object.h"
#include <string.h>

/* Two packet/framebuffer slots. Only the main thread submits GPU work;
 * the VBlank callback publishes a completed buffer and acknowledges it last.
 * A second packet list can be built while the GPU owns the first one. */
typedef struct PsxFrameQueue {
    volatile int front,gpu,ready;
    int queued;
    volatile unsigned long count,tick;
} PsxFrameQueue;

static void psx_frame_init(PsxFrameQueue *q)
{
    q->front=1;q->gpu=-1;q->ready=-1;q->queued=-1;q->count=0;q->tick=0;
}

/* Packet storage is independent of the framebuffer currently on screen.
 * Never overwrite the list owned by the GPU or the already queued list. */
static int psx_frame_build_slot(const PsxFrameQueue *q)
{
    int gpu;
    if(q->queued>=0) return -1;
    gpu=q->gpu;return 1-(gpu>=0?gpu:q->front);
}

static int psx_frame_submit(PsxFrameQueue *q)
{
    int slot;
    if(q->gpu>=0 || q->queued<0) return -1;
    slot=q->queued;q->queued=-1;q->gpu=slot;return slot;
}

static void psx_frame_gpu_done(PsxFrameQueue *q)
{
    if(q->gpu>=0 && q->ready<0) q->ready=q->gpu;
}

/* Called only after PutDispEnv() for ready, at VBlank. No game callbacks here. */
static void psx_frame_presented(PsxFrameQueue *q,unsigned long now)
{
    q->front=q->ready;q->tick=now;
    q->count=(q->count+1)&0xffffffffUL;q->ready=-1;q->gpu=-1;
}

/* Caller must first confirm that the GPU queue is idle. Keep the visible slot. */
static void psx_frame_discard(PsxFrameQueue *q)
{
    q->ready=-1;q->queued=-1;q->gpu=-1;
}

/* Immutable mesh pointers are shared; mutable object/camera/HUD values are
 * copied. The caller supplies storage for source->count objects. */
static void psx_frame_snapshot(PsxScene *frame,PsxObject *objects,const PsxScene *source)
{
    *frame=*source;
    memcpy(objects,source->objects,source->count*sizeof(PsxObject));
    frame->objects=objects;
}
#endif
