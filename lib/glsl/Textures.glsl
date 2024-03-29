#ifndef REN_GLSL_TEXTURES_GLSL
#define REN_GLSL_TEXTURES_GLSL

#include "Textures.h"

#define TEXTURES                                                               \
  layout(binding = SAMPLED_TEXTURES_SLOT)                                      \
      uniform sampler2D g_textures2d[NUM_SAMPLED_TEXTURES];                    \
                                                                               \
  layout(binding = STORAGE_TEXTURES_SLOT) restrict readonly uniform image2D    \
      g_rimages2d[NUM_STORAGE_TEXTURES];                                       \
  layout(binding = STORAGE_TEXTURES_SLOT) restrict writeonly uniform image2D   \
      g_wimages2d[NUM_STORAGE_TEXTURES];                                       \
  layout(binding = STORAGE_TEXTURES_SLOT) restrict uniform image2D             \
      g_rwimages2d[NUM_STORAGE_TEXTURES];

#endif // REN_GLSL_TEXTURES_GLSL
