// Dibujo 2D común. Cada plataforma lo implementa (software en Mac, citro2d en 3DS).
#ifndef MUT_GFX_H
#define MUT_GFX_H
#include <stdbool.h>
#include <stdint.h>

#include "pak.h"

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

enum { SCREEN_TOP = 0, SCREEN_BOTTOM = 1 };

// Matriz afín de Flash: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
typedef struct { float a, b, c, d, tx, ty; } Mat;
// Transformación de color de Flash: canal = canal*mult + add (mult 1 = identidad, add 0..255).
typedef struct { float rm, gm, bm, am, ra, ga, ba, aa; } CX;

static const Mat MAT_ID = {1, 0, 0, 1, 0, 0};
static const CX CX_ID = {1, 1, 1, 1, 0, 0, 0, 0};

static inline Mat mat_mul(const Mat *p, const Mat *c) {  // p * c: primero c, luego p
    Mat r;
    r.a = p->a * c->a + p->c * c->b;
    r.b = p->b * c->a + p->d * c->b;
    r.c = p->a * c->c + p->c * c->d;
    r.d = p->b * c->c + p->d * c->d;
    r.tx = p->a * c->tx + p->c * c->ty + p->tx;
    r.ty = p->b * c->tx + p->d * c->ty + p->ty;
    return r;
}
static inline void mat_apply(const Mat *m, float x, float y, float *ox, float *oy) {
    *ox = m->a * x + m->c * y + m->tx;
    *oy = m->b * x + m->d * y + m->ty;
}
static inline Mat mat_translate(float x, float y) { Mat m = {1, 0, 0, 1, x, y}; return m; }
static inline Mat mat_scale(float s, float x, float y) { Mat m = {s, 0, 0, s, x, y}; return m; }
static inline CX cx_mul(const CX *p, const CX *c) {  // aplica c y después p
    CX r = {c->rm * p->rm, c->gm * p->gm, c->bm * p->bm, c->am * p->am,
            c->ra * p->rm + p->ra, c->ga * p->gm + p->ga, c->ba * p->bm + p->ba, c->aa * p->am + p->aa};
    return r;
}

void gfx_upload_pages(Pak *p);
void gfx_begin(int screen);
void gfx_clear(uint32_t argb);
// Trozo (u,v,w,h) de una página, situado en (dx,dy) del espacio local y transformado por m.
void gfx_image(int page, int u, int v, int w, int h, float dx, float dy, const Mat *m, const CX *cx);
// Polígono convexo en espacio local, color ARGB.
void gfx_poly(const float *xy, int n, uint32_t argb, const Mat *m);
void gfx_line(float x0, float y0, float x1, float y1, float width, uint32_t argb, const Mat *m);
void gfx_end(void);
// Máscaras: begin = lo que se dibuje marca la máscara (sin pintar); use = lo siguiente solo se
// pinta dentro de la máscara; end = vuelta a lo normal.
void gfx_mask_begin(void);
void gfx_mask_use(void);
void gfx_mask_end(void);

#endif
