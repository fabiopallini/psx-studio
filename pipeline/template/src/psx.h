#ifndef STUDIO_PSX_H
#define STUDIO_PSX_H

#include <sys/types.h>
#include <libetc.h>
#include <libgte.h>
#include <libgpu.h>
#include "object.h"

#define STUDIO_SCREEN_WIDTH 320
#define STUDIO_SCREEN_HEIGHT 256
#define STUDIO_DRAW_LIMIT 512
/* Native builds specialize object work buffers to the largest authored Scene. */
#define STUDIO_OBJECT_LIMIT 512

extern unsigned long studio_pad;
void psInit(void);
void psShowLoading(const char *message); /* Explicit I/O boundary, not the frame loop. */
int psBeginFrame(void); /* Returns immediately; zero while both slots are owned. */
int psUpdate(void); /* Service elapsed simulation ticks and fresh pad input. */
int psGpuIdle(void); /* Safe point for Scene cleanup and texture replacement. */
const PsxScene *psDrawScene(void); /* Render a snapshot, yielding to psUpdate(). */
void psDisplay(void);
void psDrawHud(void);
void psDrawObject(const PsxObject *object, unsigned int index);
int psSetScene(PsxScene *scene, PsxTexture *textures, unsigned int count);
int psFontEnabled(void);
int psFrameRate(void); /* Tenths of FPS, -1 during the first sample window. */

#endif
