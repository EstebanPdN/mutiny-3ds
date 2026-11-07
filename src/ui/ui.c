#include "ui.h"

#include <stdio.h>
#include <string.h>

typedef struct { char key[72]; MC *mc; } Entry;

static MC *root;
static Entry cache[768];
static int ncache;
static Input cur;
static bool touching, was_touching, began, ended;
static int tx, ty, sx, sy, lx, ly;

void ui_init(void) {
    if (!root) root = mc_new_root();
}

void ui_tick(void) { if (root) mc_tick(root); }

void ui_input(const Input *in) {
    cur = *in;
    was_touching = touching;
    touching = in->touch;
    began = touching && !was_touching;
    ended = !touching && was_touching;
    if (touching) { tx = in->tx; ty = in->ty; lx = tx; ly = ty; }
    if (began) { sx = tx; sy = ty; }
}

const Input *ui_in(void) { return &cur; }

static MC *find(const char *key) {
    for (int i = 0; i < ncache; i++)
        if (!strcmp(cache[i].key, key)) return cache[i].mc;
    return NULL;
}

static MC *store(const char *key, MC *m) {
    if (!m || ncache >= 768) return m;
    snprintf(cache[ncache].key, sizeof cache[0].key, "%s", key);
    cache[ncache].mc = m;
    ncache++;
    return m;
}

MC *ui_mc(const char *key, const char *link) {
    MC *m = find(key);
    if (m) return m;
    return store(key, mc_attach(root, link, key, ncache + 1));
}

MC *ui_mc_id(const char *key, int ch) {
    MC *m = find(key);
    if (m) return m;
    return store(key, mc_attach_id(root, ch, key, ncache + 1));
}

MC *ui_textmc(const char *key, int font, int align, int tracking, int spacing) {
    MC *m = find(key);
    if (m) return m;
    return store(key, mc_text_create(root, key, ncache + 1, font, align, tracking, spacing));
}

void ui_draw_m(MC *mc, const Mat *m, const CX *cx) {
    if (!mc) return;
    mc_render(mc, m, cx ? cx : &CX_ID);
}

void ui_draw_cx(MC *mc, float x, float y, const CX *cx) {
    if (!mc) return;
    Mat m = mat_translate(x - mc->m.tx, y - mc->m.ty);
    mc_render(mc, &m, cx ? cx : &CX_ID);
}

void ui_draw(MC *mc, float x, float y) { ui_draw_cx(mc, x, y, NULL); }

void ui_sprite(const char *key, const char *link, const char *label, float x, float y) {
    MC *m = ui_mc(key, link);
    if (!m) return;
    if (label) mc_goto_label(m, label, false);
    ui_draw(m, x, y);
}

static int font_tracking(int font) { return font == FONT_PIRATE ? -3 : 0; }
static int font_spacing(int font) { return font == FONT_PIRATE ? 9 : 13; }

void ui_text_cx(const char *key, int font, int align, float x, float y, const char *text, const CX *cx) {
    MC *t = ui_textmc(key, font, align, font_tracking(font), font_spacing(font));
    mc_set_text(t, text);
    ui_draw_cx(t, x, y, cx);
}

void ui_text(const char *key, int font, int align, float x, float y, const char *text) {
    ui_text_cx(key, font, align, x, y, text, NULL);
}

float ui_text_width(const char *key, int font, const char *text) {
    MC *t = ui_textmc(key, font, ALIGN_LEFT, font_tracking(font), font_spacing(font));
    mc_set_text(t, text);
    float b[4];
    if (!mc_bounds(t, b)) return 0;
    return b[2] - b[0];
}

void ui_rect(float x, float y, float w, float h, uint32_t argb) {
    float xy[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    gfx_poly(xy, 4, argb, &MAT_ID);
}

void ui_line(float x0, float y0, float x1, float y1, float w, uint32_t argb) { gfx_line(x0, y0, x1, y1, w, argb, &MAT_ID); }

void ui_frame(float x, float y, float w, float h, uint32_t c) {
    ui_rect(x, y, w, 1, c);
    ui_rect(x, y + h - 1, w, 1, c);
    ui_rect(x, y + 1, 1, h - 2, c);
    ui_rect(x + w - 1, y + 1, 1, h - 2, c);
}

void ui_nine(int shape_ch, int in, float x, float y, float w, float h, const CX *cx) {
    const Pak *P = mc_pak();
    const PakShape *s = pak_shape(P, shape_ch);
    if (!s || !s->n) return;
    const PakQuad *q = &P->quads[s->first];
    if (!cx) cx = &CX_ID;
    int sw = q->w, sh = q->h;
    int xs[4] = {0, in, sw - in, sw}, ys[4] = {0, in, sh - in, sh};
    float dxs[4] = {0, (float)in, w - in, w}, dys[4] = {0, (float)in, h - in, h};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            int u = q->u + xs[i], v = q->v + ys[j], uw = xs[i + 1] - xs[i], vh = ys[j + 1] - ys[j];
            float dw = dxs[i + 1] - dxs[i], dh = dys[j + 1] - dys[j];
            if (uw <= 0 || vh <= 0 || dw <= 0 || dh <= 0) continue;
            Mat m = {dw / uw, 0, 0, dh / vh, x + dxs[i], y + dys[j]};
            gfx_image(q->page, u, v, uw, vh, 0, 0, &m, cx);
        }
}

void ui_image_cx(const char *name, float x, float y, const CX *cx) {
    char key[64], link[64];
    snprintf(key, sizeof key, "img:%s", name);
    snprintf(link, sizeof link, "ui_%s", name);
    MC *m = ui_mc(key, link);
    if (m) ui_draw_cx(m, x, y, cx);
}

void ui_image(const char *name, float x, float y) { ui_image_cx(name, x, y, NULL); }

bool ui_touching(void) { return touching; }
int ui_tx(void) { return touching ? tx : lx; }
int ui_ty(void) { return touching ? ty : ly; }
int ui_start_x(void) { return sx; }
int ui_start_y(void) { return sy; }
bool ui_touch_began_any(void) { return began; }
bool ui_touch_ended(void) { return ended; }

static bool inside(int px, int py, float x, float y, float w, float h) { return px >= x && py >= y && px < x + w && py < y + h; }

bool ui_hover(float x, float y, float w, float h) { return touching && inside(tx, ty, x, y, w, h) && inside(sx, sy, x, y, w, h); }
bool ui_touch_began(float x, float y, float w, float h) { return began && inside(tx, ty, x, y, w, h); }
bool ui_tap(float x, float y, float w, float h) { return ended && inside(lx, ly, x, y, w, h) && inside(sx, sy, x, y, w, h); }
