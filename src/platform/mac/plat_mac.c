// macOS: SDL3. Una ventana con las dos pantallas de la 3DS (400x240 arriba, 320x240 abajo
// centrada), escalado entero para que los píxeles salgan perfectos. El ratón en la pantalla
// de abajo hace de stylus.
//
// Teclado (como el port de Worms): flechas = cruceta, WASD = circle pad, Espacio = A,
// Mayús izq. = B, F = X, G = Y, Q = L, E = R, Intro = START, Tab = SELECT.
//
// Pruebas sin manos:
//   MUT_HIDDEN=1        sin ventana y sin esperar al reloj
//   MUT_INPUT="<cuadro>:<orden>;..."  órdenes: k<BOTÓN> (A B X Y L R S=start E=select U D I=izq T=der),
//       t<x>,<y> toque, d<x0>,<y0>,<x1>,<y1>,<cuadros> arrastre, h<x>,<y>,<cuadros> mantener,
//       s<nombre> captura en build/shots/<nombre>.png, q salir
//   MUT_DATA=<dir>      carpeta de mutiny.pak
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../platform.h"
#include "../script.h"
#include "../soft/soft_gfx.h"

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex_top, *tex_bot;
static SDL_Gamepad *pad;
static SDL_AudioStream *astream;
static AudioPull apull;
static bool hidden;
static int frame_no;
static uint32_t prev_held;
static char data_dir[1024] = "data";
static char shots_dir[1024] = "build/shots";

#define SCALE_MAX 8

static void layout(int w, int h, int *scale, int *ox, int *oy) {
    int s = w / 400 < h / 480 ? w / 400 : h / 480;
    if (s < 1) s = 1;
    if (s > SCALE_MAX) s = SCALE_MAX;
    *scale = s;
    *ox = (w - 400 * s) / 2;
    *oy = (h - 480 * s) / 2;
}

bool plat_init(const char *title) {
    hidden = getenv("MUT_HIDDEN") != NULL;
    const char *dd = getenv("MUT_DATA");
    if (dd) snprintf(data_dir, sizeof data_dir, "%s", dd);
    else {
        const char *base = SDL_GetBasePath();
        char tryp[1100];
        snprintf(tryp, sizeof tryp, "%s../data/mutiny.pak", base ? base : "");
        FILE *f = fopen(tryp, "rb");
        if (f) { fclose(f); snprintf(data_dir, sizeof data_dir, "%s../data", base); }
        snprintf(shots_dir, sizeof shots_dir, "%sshots", base ? base : "build/");
    }
    if (!SDL_Init(SDL_INIT_AUDIO | (hidden ? 0 : SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (hidden) return true;
    win = SDL_CreateWindow(title, 800, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) return false;
    ren = SDL_CreateRenderer(win, NULL);
    SDL_SetRenderVSync(ren, 1);
    tex_top = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, TOP_W, TOP_H);
    tex_bot = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, BOT_W, BOT_H);
    SDL_SetTextureScaleMode(tex_top, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(tex_bot, SDL_SCALEMODE_NEAREST);
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids && n > 0) pad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
    return true;
}

void plat_shutdown(void) {
    if (astream) SDL_DestroyAudioStream(astream);
    if (pad) SDL_CloseGamepad(pad);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

static bool want_quit;
static bool script_loaded;

static void apply_script(Input *in) {
    if (!script_loaded) {
        script_loaded = true;
        script_load(getenv("MUT_INPUT"));
    }
    char shot[64];
    if (!script_apply(in, frame_no, shot, sizeof shot)) want_quit = true;
    if (shot[0]) {
        char p[1200];
        snprintf(p, sizeof p, "%s/%s.png", shots_dir, shot);
        plat_screenshot(p);
    }
}

bool plat_poll(Input *in) {
    memset(in, 0, sizeof *in);
    if (!hidden) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) return false;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && (e.key.mod & SDL_KMOD_GUI)) return false;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
        }
        const bool *k = SDL_GetKeyboardState(NULL);
        struct { SDL_Scancode sc; uint32_t b; } map[] = {
            {SDL_SCANCODE_SPACE, BTN_A}, {SDL_SCANCODE_LSHIFT, BTN_B}, {SDL_SCANCODE_F, BTN_X}, {SDL_SCANCODE_G, BTN_Y},
            {SDL_SCANCODE_Q, BTN_L}, {SDL_SCANCODE_E, BTN_R}, {SDL_SCANCODE_RETURN, BTN_START}, {SDL_SCANCODE_TAB, BTN_SELECT},
            {SDL_SCANCODE_UP, BTN_UP}, {SDL_SCANCODE_DOWN, BTN_DOWN}, {SDL_SCANCODE_LEFT, BTN_LEFT}, {SDL_SCANCODE_RIGHT, BTN_RIGHT},
            {SDL_SCANCODE_Z, BTN_ZL}, {SDL_SCANCODE_C, BTN_ZR},
        };
        for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
            if (k[map[i].sc]) in->held |= map[i].b;
        in->cx = (k[SDL_SCANCODE_D] ? 1.0f : 0) - (k[SDL_SCANCODE_A] ? 1.0f : 0);
        in->cy = (k[SDL_SCANCODE_S] ? 1.0f : 0) - (k[SDL_SCANCODE_W] ? 1.0f : 0);
        if (pad && SDL_GamepadConnected(pad)) {
            // Posiciones físicas de Nintendo: A a la derecha, B abajo
            struct { SDL_GamepadButton gb; uint32_t b; } gm[] = {
                {SDL_GAMEPAD_BUTTON_EAST, BTN_A}, {SDL_GAMEPAD_BUTTON_SOUTH, BTN_B}, {SDL_GAMEPAD_BUTTON_NORTH, BTN_X},
                {SDL_GAMEPAD_BUTTON_WEST, BTN_Y}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, BTN_L}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, BTN_R},
                {SDL_GAMEPAD_BUTTON_START, BTN_START}, {SDL_GAMEPAD_BUTTON_BACK, BTN_SELECT},
                {SDL_GAMEPAD_BUTTON_DPAD_UP, BTN_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, BTN_DOWN},
                {SDL_GAMEPAD_BUTTON_DPAD_LEFT, BTN_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, BTN_RIGHT},
            };
            for (size_t i = 0; i < sizeof gm / sizeof gm[0]; i++)
                if (SDL_GetGamepadButton(pad, gm[i].gb)) in->held |= gm[i].b;
            if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) in->held |= BTN_ZL;
            if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) in->held |= BTN_ZR;
            float ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
            float ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
            if (ax * ax + ay * ay > 0.04f) { in->cx = ax; in->cy = ay; }
        }
        float mx, my;
        SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
        int ww, wh, pw, ph;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        int s, ox, oy;
        layout(pw, ph, &s, &ox, &oy);
        float px = mx * pw / (float)ww, py = my * ph / (float)wh;
        int tx = (int)((px - ox) / s) - 40, ty = (int)((py - oy) / s) - 240;
        if ((mb & SDL_BUTTON_LMASK) && tx >= 0 && tx < 320 && ty >= 0 && ty < 240) {
            in->touch = true;
            in->tx = tx;
            in->ty = ty;
        }
    }
    apply_script(in);
    if (in->touch) in->held |= BTN_TOUCH;
    in->down = in->held & ~prev_held;
    in->up = prev_held & ~in->held;
    prev_held = in->held;
    return !want_quit;
}

void plat_present(void) {
    frame_no++;
    if (hidden) return;
    SDL_UpdateTexture(tex_top, NULL, soft_fb(SCREEN_TOP), TOP_W * 4);
    SDL_UpdateTexture(tex_bot, NULL, soft_fb(SCREEN_BOTTOM), BOT_W * 4);
    int pw, ph, s, ox, oy;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    layout(pw, ph, &s, &ox, &oy);
    SDL_SetRenderDrawColor(ren, 18, 16, 24, 255);
    SDL_RenderClear(ren);
    SDL_FRect rt = {(float)ox, (float)oy, 400.0f * s, 240.0f * s};
    SDL_FRect rb = {(float)(ox + 40 * s), (float)(oy + 240 * s), 320.0f * s, 240.0f * s};
    SDL_RenderTexture(ren, tex_top, NULL, &rt);
    SDL_RenderTexture(ren, tex_bot, NULL, &rb);
    SDL_RenderPresent(ren);
}

double plat_time(void) { return SDL_GetTicksNS() / 1e9; }
void plat_sleep(double s) { if (!hidden && s > 0) SDL_DelayNS((Uint64)(s * 1e9)); }
int plat_frame(void) { return frame_no; }

// ------------------------------------------------------------ archivos
void *plat_load_data(const char *name, size_t *size) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", data_dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) { fprintf(stderr, "[plat] no se encuentra %s\n", p); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static const char *save_dir(void) {
    static char dir[1024];
    if (!dir[0]) {
        const char *env = getenv("MUT_SAVE");
        char *pp = env ? NULL : SDL_GetPrefPath("Nitrome", "Mutiny3DS");
        snprintf(dir, sizeof dir, "%s", env ? env : pp ? pp : "./");
        SDL_free(pp);
    }
    return dir;
}

bool plat_save_write(const char *name, const void *data, size_t size) {
    char p[1200], tmp[1210];
    snprintf(p, sizeof p, "%s/%s", save_dir(), name);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    return ok && rename(tmp, p) == 0;
}

void *plat_save_read(const char *name, size_t *size) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", save_dir(), name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    *size = got;
    return buf;
}

// ------------------------------------------------------------ audio
static void SDLCALL audio_cb(void *ud, SDL_AudioStream *st, int additional, int total) {
    (void)ud; (void)total;
    int16_t buf[2048 * 2];
    while (additional > 0) {
        int frames = additional / 4;
        if (frames > 2048) frames = 2048;
        if (frames <= 0) break;
        apull(buf, frames);
        SDL_PutAudioStreamData(st, buf, frames * 4);
        additional -= frames * 4;
    }
}

bool plat_audio_start(int rate, AudioPull pull) {
    if (getenv("MUT_MUTE")) return false;
    apull = pull;
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, rate};
    astream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, NULL);
    if (!astream) { fprintf(stderr, "[audio] %s\n", SDL_GetError()); return false; }
    SDL_ResumeAudioStreamDevice(astream);
    return true;
}

int plat_audio_rate(void) { return 44100; }
void plat_audio_lock(void) { if (astream) SDL_LockAudioStream(astream); }
void plat_audio_unlock(void) { if (astream) SDL_UnlockAudioStream(astream); }

// ------------------------------------------------------------ capturas PNG
void plat_screenshot(const char *path) {
    const int W = 400, H = 480;
    uint8_t *rgb = malloc((size_t)W * H * 3);
    const uint32_t *top = soft_fb(SCREEN_TOP), *bot = soft_fb(SCREEN_BOTTOM);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = 0xFF121018;
            if (y < 240) c = top[y * 400 + x];
            else if (x >= 40 && x < 360) c = bot[(y - 240) * 320 + x - 40];
            uint8_t *d = rgb + ((size_t)y * W + x) * 3;
            d[0] = (c >> 16) & 255; d[1] = (c >> 8) & 255; d[2] = c & 255;
        }
    char dir[1200];
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = 0; mkdir(dir, 0755); }
    if (png_write_rgb(path, rgb, W, H)) fprintf(stderr, "[plat] captura %s\n", path);
    free(rgb);
}
