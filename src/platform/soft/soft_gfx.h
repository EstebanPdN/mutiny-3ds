#ifndef MUT_SOFT_GFX_H
#define MUT_SOFT_GFX_H
#include "../../core/gfx.h"

// Framebuffer ARGB de cada pantalla (400x240 arriba, 320x240 abajo).
const uint32_t *soft_fb(int screen);

#endif
