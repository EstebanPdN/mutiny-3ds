// Implementación por software de gfx.h: dos framebuffers ARGB (400x240 y 320x240),
// muestreo al vecino más cercano en el centro del píxel, como Flash con _quality LOW.
#include "soft_gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fb_top[TOP_W * TOP_H];
static uint32_t fb_bot[BOT_W * BOT_H];
static uint8_t mk_top[TOP_W * TOP_H], mk_bot[BOT_W * BOT_H];
static uint8_t *mk = mk_top;
static int mask_mode;  // 0 normal, 1 escribiendo la máscara, 2 recortando
static uint32_t *fb = fb_top;
static int fbw = TOP_W, fbh = TOP_H;
static uint32_t **pages;
static int npages, pagew[512], pageh[512];

const uint32_t *soft_fb(int screen) { return screen == SCREEN_TOP ? fb_top : fb_bot; }

void gfx_upload_pages(Pak *p) {
    pages = calloc((size_t)p->npages, sizeof *pages);
    npages = p->npages;
    for (int i = 0; i < p->npages && i < 512; i++) {
        int w = p->pages[i].w, h = p->pages[i].h;
        const uint8_t *src = pak_page_pixels(p, i);
        if (!src) continue;
        uint32_t *dst = malloc(sizeof *dst * (size_t)w * h);
        for (int k = 0; k < w * h; k++)
            dst[k] = ((uint32_t)src[4 * k + 3] << 24) | ((uint32_t)src[4 * k] << 16) | ((uint32_t)src[4 * k + 1] << 8) | src[4 * k + 2];
        pages[i] = dst;
        pagew[i] = w;
        pageh[i] = h;
        pak_page_release(p, i);
    }
}

void gfx_begin(int screen) {
    if (screen == SCREEN_TOP) { fb = fb_top; mk = mk_top; fbw = TOP_W; fbh = TOP_H; }
    else { fb = fb_bot; mk = mk_bot; fbw = BOT_W; fbh = BOT_H; }
    mask_mode = 0;
}

void gfx_end(void) {}

void gfx_clear(uint32_t argb) {
    for (int i = 0; i < fbw * fbh; i++) fb[i] = argb | 0xFF000000u;
}

static inline uint32_t blend(uint32_t dst, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    if (a >= 255) return 0xFF000000u | (r << 16) | (g << 8) | b;
    uint32_t ia = 255 - a;
    uint32_t dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
    dr = (r * a + dr * ia + 127) / 255;
    dg = (g * a + dg * ia + 127) / 255;
    db = (b * a + db * ia + 127) / 255;
    return 0xFF000000u | (dr << 16) | (dg << 8) | db;
}

static inline int clampi(float v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f); }

void gfx_mask_begin(void) {
    memset(mk, 0, (size_t)fbw * fbh);
    mask_mode = 1;
}
void gfx_mask_use(void) { mask_mode = 2; }
void gfx_mask_end(void) { mask_mode = 0; }

static inline void put(uint32_t *d, uint32_t s, const CX *cx, bool ident) {
    uint32_t a = s >> 24;
    if (mask_mode) {
        uint8_t *m = mk + (d - fb);
        if (mask_mode == 1) { if (a) *m = 1; return; }
        if (!*m) return;
    }
    if (ident) {
        if (!a) return;
        *d = blend(*d, (s >> 16) & 255, (s >> 8) & 255, s & 255, a);
        return;
    }
    int na = clampi(a * cx->am + cx->aa);
    if (!a || na <= 0) return;
    *d = blend(*d, (uint32_t)clampi(((s >> 16) & 255) * cx->rm + cx->ra), (uint32_t)clampi(((s >> 8) & 255) * cx->gm + cx->ga),
               (uint32_t)clampi((s & 255) * cx->bm + cx->ba), (uint32_t)na);
}

void gfx_image(int page, int u, int v, int w, int h, float dx, float dy, const Mat *m, const CX *cx) {
    if (page < 0 || page >= npages || w <= 0 || h <= 0) return;
    const uint32_t *tex = pages[page];
    int tw = pagew[page];
    bool ident = cx->rm == 1 && cx->gm == 1 && cx->bm == 1 && cx->am == 1 && cx->ra == 0 && cx->ga == 0 && cx->ba == 0 && cx->aa == 0;
    if (m->a == 1 && m->d == 1 && m->b == 0 && m->c == 0) {
        float ox = m->tx + dx, oy = m->ty + dy;
        int x0 = (int)ceilf(ox - 0.5f), y0 = (int)ceilf(oy - 0.5f);
        int sx0 = 0, sy0 = 0, ww = w, hh = h;
        if (x0 < 0) { sx0 = -x0; ww += x0; x0 = 0; }
        if (y0 < 0) { sy0 = -y0; hh += y0; y0 = 0; }
        if (x0 + ww > fbw) ww = fbw - x0;
        if (y0 + hh > fbh) hh = fbh - y0;
        for (int yy = 0; yy < hh; yy++) {
            const uint32_t *s = tex + (size_t)(v + sy0 + yy) * tw + u + sx0;
            uint32_t *d = fb + (size_t)(y0 + yy) * fbw + x0;
            for (int xx = 0; xx < ww; xx++) put(d + xx, s[xx], cx, ident);
        }
        return;
    }
    float xs[4] = {dx, dx + w, dx + w, dx}, ys[4] = {dy, dy, dy + h, dy + h};
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < 4; i++) {
        float X, Y;
        mat_apply(m, xs[i], ys[i], &X, &Y);
        minx = fminf(minx, X); maxx = fmaxf(maxx, X);
        miny = fminf(miny, Y); maxy = fmaxf(maxy, Y);
    }
    int px0 = (int)floorf(minx), px1 = (int)ceilf(maxx), py0 = (int)floorf(miny), py1 = (int)ceilf(maxy);
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 > fbw) px1 = fbw;
    if (py1 > fbh) py1 = fbh;
    float det = m->a * m->d - m->b * m->c;
    if (fabsf(det) < 1e-9f) return;
    float ia = m->d / det, ib = -m->b / det, ic = -m->c / det, id = m->a / det;
    for (int py = py0; py < py1; py++) {
        float cy = py + 0.5f - m->ty;
        float cx0 = px0 + 0.5f - m->tx;
        float lx = ia * cx0 + ic * cy - dx, ly = ib * cx0 + id * cy - dy;
        uint32_t *d = fb + (size_t)py * fbw;
        for (int px = px0; px < px1; px++, lx += ia, ly += ib) {
            int tu = (int)floorf(lx), tv = (int)floorf(ly);
            if (tu < 0 || tv < 0 || tu >= w || tv >= h) continue;
            put(d + px, tex[(size_t)(v + tv) * tw + u + tu], cx, ident);
        }
    }
}

static void fill_screen_poly(const float *X, const float *Y, int n, uint32_t argb) {
    uint32_t a = argb >> 24;
    if (!a || n < 3) return;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f, area = 0;
    for (int i = 0; i < n; i++) {
        minx = fminf(minx, X[i]); maxx = fmaxf(maxx, X[i]);
        miny = fminf(miny, Y[i]); maxy = fmaxf(maxy, Y[i]);
        int j = (i + 1) % n;
        area += X[i] * Y[j] - X[j] * Y[i];
    }
    float sgn = area < 0 ? -1.0f : 1.0f;
    int px0 = (int)floorf(minx), px1 = (int)ceilf(maxx), py0 = (int)floorf(miny), py1 = (int)ceilf(maxy);
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 > fbw) px1 = fbw;
    if (py1 > fbh) py1 = fbh;
    uint32_t r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
    for (int py = py0; py < py1; py++)
        for (int px = px0; px < px1; px++) {
            float cxp = px + 0.5f, cyp = py + 0.5f;
            bool in = true;
            for (int i = 0; i < n && in; i++) {
                int j = (i + 1) % n;
                float e = (X[j] - X[i]) * (cyp - Y[i]) - (Y[j] - Y[i]) * (cxp - X[i]);
                if (e * sgn < 0) in = false;
            }
            if (!in) continue;
            if (mask_mode == 1) { mk[py * fbw + px] = 1; continue; }
            if (mask_mode == 2 && !mk[py * fbw + px]) continue;
            fb[py * fbw + px] = blend(fb[py * fbw + px], r, g, b, a);
        }
}

void gfx_poly(const float *xy, int n, uint32_t argb, const Mat *m) {
    float X[16], Y[16];
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) mat_apply(m, xy[2 * i], xy[2 * i + 1], &X[i], &Y[i]);
    fill_screen_poly(X, Y, n, argb);
}

void gfx_line(float x0, float y0, float x1, float y1, float width, uint32_t argb, const Mat *m) {
    float ax, ay, bx, by;
    mat_apply(m, x0, y0, &ax, &ay);
    mat_apply(m, x1, y1, &bx, &by);
    float sc = sqrtf(fabsf(m->a * m->d - m->b * m->c));
    float w = width * sc;
    if (w < 1) w = 1;
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy);
    if (L < 1e-4f) return;
    float nx = -dy / L * w * 0.5f, ny = dx / L * w * 0.5f;
    float X[4] = {ax + nx, bx + nx, bx - nx, ax - nx}, Y[4] = {ay + ny, by + ny, by - ny, ay - ny};
    fill_screen_poly(X, Y, 4, argb);
}
