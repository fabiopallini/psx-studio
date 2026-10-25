#ifndef STUDIO_TEST_PSX_H
#define STUDIO_TEST_PSX_H
#include "../../runtime/object.h"
extern unsigned long studio_pad;
void psInit(void);
void psShowLoading(const char *message); /* Explicit I/O boundary, not the frame loop. */
int psBeginFrame(void);
int psUpdate(void);
int psGpuIdle(void);
const PsxScene *psDrawScene(void);
void psDisplay(void);
void psDrawHud(void);
void psDrawObject(const PsxObject *object,unsigned int index);
int psSetScene(PsxScene *scene,PsxTexture *textures,unsigned int count);
int psFontEnabled(void);
int psFrameRate(void);
int FntPrint(const char *format,...);
#endif
