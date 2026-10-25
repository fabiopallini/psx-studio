#ifndef STUDIO_TEST_SCENE_H
#define STUDIO_TEST_SCENE_H
#include "psx.h"
extern PsxTexture studio_textures[1];
extern unsigned int studio_texture_count;
extern const char *studio_scene_name;
PsxScene *studio_scene_load(void);
int studio_scene_apply_change(void);
#endif
