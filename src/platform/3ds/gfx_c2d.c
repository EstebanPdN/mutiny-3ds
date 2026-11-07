// gfx.h con citro2d. Las páginas del pak vienen ya en formato de la GPU (RGBA8 en baldosas
// Morton) y se suben a la memoria lineal solo cuando se dibuja algo de ellas; si falta memoria
// se libera la página usada hace más tiempo. Muestreo al vecino más cercano (pixel art).
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/gfx.h"

#define MAXP 512

typedef struct {
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    bool loaded;
    uint32_t last_used;
} Page;

static Pak *P;
static Page pages[MAXP];
static uint32_t frame_counter;
static C3D_RenderTarget *top, *bot;
static int cur_screen = -1;
static bool frame_open;
static float depth;

#define DISPLAY_TRANSFER_FLAGS                                                                          \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static bool cleared[2];
static int mask_ref;

void gfx3ds_init(void) {
    // Como C2D_CreateScreenTarget, pero con profundidad+stencil seguro (las máscaras lo usan)
    top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    bot = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(bot, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
}

void gfx_mask_begin(void) {
    C2D_Flush();
    if (++mask_ref > 255) mask_ref = 1;
    C3D_StencilTest(true, GPU_ALWAYS, mask_ref, 0xFF, 0xFF);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_DEPTH);  // sin color: solo marca el stencil
}

void gfx_mask_use(void) {
    C2D_Flush();
    C3D_StencilTest(true, GPU_EQUAL, mask_ref, 0xFF, 0x00);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
}

void gfx_mask_end(void) {
    C2D_Flush();
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0x00);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
}

void gfx_upload_pages(Pak *p) { P = p; }

static void page_free(int i) {
    if (!pages[i].loaded) return;
    C3D_TexDelete(&pages[i].tex);
    pages[i].loaded = false;
}

static bool page_load(int i) {
    const uint8_t *px = pak_page_pixels(P, i);
    if (!px) return false;
    int w = P->pages[i].w, h = P->pages[i].h;
    while (!C3D_TexInit(&pages[i].tex, w, h, GPU_RGBA8)) {
        // Sin memoria lineal: fuera la página menos usada
        int victim = -1;
        for (int k = 0; k < MAXP; k++)
            if (pages[k].loaded && (victim < 0 || pages[k].last_used < pages[victim].last_used)) victim = k;
        if (victim < 0) {
            fprintf(stderr, "[gfx] sin memoria para la página %d\n", i);
            pak_page_release(P, i);
            return false;
        }
        fprintf(stderr, "[gfx] libero la página %d para cargar la %d\n", victim, i);
        if (frame_open) {
            // La GPU puede estar leyendo la víctima: cerrar el cuadro y esperar a que termine
            C3D_FrameEnd(0);
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            if (cur_screen >= 0) C2D_SceneBegin(cur_screen == SCREEN_TOP ? top : bot);
        }
        page_free(victim);
    }
    memcpy(pages[i].tex.data, px, (size_t)w * h * 4);
    C3D_TexFlush(&pages[i].tex);
    C3D_TexSetFilter(&pages[i].tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&pages[i].tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    pak_page_release(P, i);
    pages[i].loaded = true;
    return true;
}

static void ensure_frame(void) {
    if (!frame_open) {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        frame_open = true;
        cur_screen = -1;
    }
}

void gfx_begin(int screen) {
    ensure_frame();
    C3D_RenderTarget *t = screen == SCREEN_TOP ? top : bot;
    if (!cleared[screen]) {
        // Una vez por cuadro: color y stencil a cero (las referencias de máscara vuelven a empezar)
        C2D_TargetClear(t, C2D_Color32(0, 0, 0, 255));
        cleared[screen] = true;
    }
    C2D_SceneBegin(t);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
    cur_screen = screen;
    mask_ref = 0;
    depth = 0.5f;
}

void gfx_end(void) {}

static char capture[64];

void gfx3ds_capture_next(char *name) { snprintf(capture, sizeof capture, "%s", name); }

bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);

// Lee la imagen que acaba de salir a las pantallas: el framebuffer de destino de la copia de
// citro3d es el que devuelve gfxGetFramebuffer antes de C3D_FrameEnd. BGR8 en columnas giradas.
static void save_capture(u8 *ft, u8 *fb) {
    const int W = 400, H = 480;
    uint8_t *rgb = calloc((size_t)W * H, 3);
    if (!rgb) return;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 400; x++) {
            const u8 *s = ft + ((size_t)x * 240 + (239 - y)) * 3;
            uint8_t *d = rgb + ((size_t)y * W + x) * 3;
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
        }
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 400; x++) {
            uint8_t *d = rgb + ((size_t)(y + 240) * W + x) * 3;
            if (x < 40 || x >= 360) { d[0] = 0x18; d[1] = 0x10; d[2] = 0x12; continue; }
            const u8 *s = fb + ((size_t)(x - 40) * 240 + (239 - y)) * 3;
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
        }
    char path[128];
    snprintf(path, sizeof path, "sdmc:/3ds/mutiny/shots/%s.png", capture);
    png_write_rgb(path, rgb, W, H);
    free(rgb);
    fprintf(stderr, "[3ds] captura %s\n", path);
}

void gfx3ds_present(void) {
    u8 *ft = NULL, *fb = NULL;
    if (capture[0]) {
        ft = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    }
    if (frame_open) C3D_FrameEnd(0);
    frame_open = false;
    frame_counter++;
    cleared[0] = cleared[1] = false;
    if (capture[0]) {
        // Esperar a que la GPU termine y la copia llegue a la pantalla
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C3D_FrameEnd(0);
        gspWaitForVBlank();
        gspWaitForVBlank();
        save_capture(ft, fb);
        capture[0] = 0;
    }
}

static inline u32 abgr(uint32_t argb) {
    return ((argb >> 24) << 24) | ((argb & 0xFF) << 16) | (argb & 0xFF00) | ((argb >> 16) & 0xFF);
}

void gfx_clear(uint32_t argb) {
    C2D_TargetClear(cur_screen == SCREEN_TOP ? top : bot, abgr(argb | 0xFF000000u));
    C2D_SceneBegin(cur_screen == SCREEN_TOP ? top : bot);
}

static void tint_from_cx(const CX *cx, C2D_ImageTint *t, bool *use) {
    *use = false;
    float a = cx->am + cx->aa / 255.0f;
    if (a > 1) a = 1;
    if (a < 0) a = 0;
    float m = (cx->rm + cx->gm + cx->bm) / 3.0f;
    bool col = !(cx->rm == 1 && cx->gm == 1 && cx->bm == 1 && cx->ra == 0 && cx->ga == 0 && cx->ba == 0);
    if (!col && a >= 1) return;
    *use = true;
    if (!col) {
        C2D_AlphaImageTint(t, a);
        return;
    }
    // canal*m + add  ==  mezcla hacia el color add/(1-m) con peso (1-m)
    float blend = 1 - m;
    if (blend < 0) blend = 0;
    if (blend > 1) blend = 1;
    float k = blend > 0.001f ? 1.0f / blend : 0;
    float r = cx->ra * k, g = cx->ga * k, b = cx->ba * k;
    if (blend <= 0.001f) { r = g = b = 0; }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    C2D_PlainImageTint(t, C2D_Color32f(r / 255, g / 255, b / 255, a), blend);
}

void gfx_image(int page, int u, int v, int w, int h, float dx, float dy, const Mat *m, const CX *cx) {
    if (page < 0 || page >= MAXP || page >= P->npages) return;
    Page *pg = &pages[page];
    if (!pg->loaded && !page_load(page)) return;
    ensure_frame();
    pg->last_used = frame_counter;
    float W = pg->tex.width, H = pg->tex.height;
    pg->sub.width = (u16)w;
    pg->sub.height = (u16)h;
    pg->sub.left = u / W;
    pg->sub.right = (u + w) / W;
    pg->sub.top = 1.0f - v / H;
    pg->sub.bottom = 1.0f - (v + h) / H;
    C2D_Image img = {&pg->tex, &pg->sub};
    // Matriz de Flash = traslación * giro * escala (sin sesgo, que el juego no usa)
    float sx = sqrtf(m->a * m->a + m->b * m->b);
    float ang = atan2f(m->b, m->a);
    float sy = sx > 1e-6f ? (m->a * m->d - m->b * m->c) / sx : sqrtf(m->c * m->c + m->d * m->d);
    C2D_DrawParams p;
    p.pos.x = m->tx;
    p.pos.y = m->ty;
    p.pos.w = w * sx;
    p.pos.h = h * sy;
    p.center.x = -dx * sx;
    p.center.y = -dy * sy;
    p.depth = depth;
    p.angle = ang;
    if (ang == 0 && sx == 1 && sy == 1) {
        // Sin giro ni escala: posición entera, como el muestreo por centro de píxel del Mac
        p.pos.x = ceilf(m->tx + dx - 0.5f) - dx;
        p.pos.y = ceilf(m->ty + dy - 0.5f) - dy;
    }
    C2D_ImageTint t;
    bool use;
    tint_from_cx(cx, &t, &use);
    C2D_DrawImage(img, &p, use ? &t : NULL);
}

void gfx_poly(const float *xy, int n, uint32_t argb, const Mat *mt) {
    if (n < 3 || !(argb >> 24)) return;
    ensure_frame();
    u32 c = abgr(argb);
    float X[16], Y[16];
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) mat_apply(mt, xy[2 * i], xy[2 * i + 1], &X[i], &Y[i]);
    // Rectángulo alineado: C2D_DrawRectSolid es más barato
    if (n == 4 && Y[0] == Y[1] && X[1] == X[2] && Y[2] == Y[3] && X[3] == X[0]) {
        float x0 = fminf(X[0], X[1]), y0 = fminf(Y[0], Y[2]);
        C2D_DrawRectSolid(x0, y0, depth, fabsf(X[1] - X[0]), fabsf(Y[2] - Y[0]), c);
        return;
    }
    for (int i = 1; i + 1 < n; i++) C2D_DrawTriangle(X[0], Y[0], c, X[i], Y[i], c, X[i + 1], Y[i + 1], c, depth);
}

void gfx_line(float x0, float y0, float x1, float y1, float width, uint32_t argb, const Mat *m) {
    float ax, ay, bx, by;
    mat_apply(m, x0, y0, &ax, &ay);
    mat_apply(m, x1, y1, &bx, &by);
    float sc = sqrtf(fabsf(m->a * m->d - m->b * m->c));
    float w = width * sc;
    if (w < 1) w = 1;
    float ddx = bx - ax, ddy = by - ay, L = sqrtf(ddx * ddx + ddy * ddy);
    if (L < 1e-4f || !(argb >> 24)) return;
    ensure_frame();
    float nx = -ddy / L * w * 0.5f, ny = ddx / L * w * 0.5f;
    u32 c = abgr(argb);
    C2D_DrawTriangle(ax + nx, ay + ny, c, bx + nx, by + ny, c, bx - nx, by - ny, c, depth);
    C2D_DrawTriangle(ax + nx, ay + ny, c, bx - nx, by - ny, c, ax - nx, ay - ny, c, depth);
}
