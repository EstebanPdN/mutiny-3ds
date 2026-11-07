// Nintendo 3DS: libctru + citro2d + ndsp. Datos en romfs:/, partida en sdmc:/3ds/mutiny/.
// Audio: el mezclador del juego rellena búferes de ndsp desde el hilo principal.
#include <3ds.h>
#include <citro2d.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../platform.h"
#include "../script.h"

void gfx3ds_init(void);
void gfx3ds_present(void);

// Memoria lineal para texturas (citro3d) y audio; el resto de la memoria de la aplicación
// queda para el montón normal (pak descomprimido, unos 8 MB, y el juego).
u32 __ctru_linear_heap_size = 40 * 1024 * 1024;

#define AUDIO_RATE 32728
#define AUDIO_FRAMES 1024
#define AUDIO_BUFS 4

static int frame_no;
static bool audio_ok;
static AudioPull apull;
static ndspWaveBuf wbuf[AUDIO_BUFS];
static int16_t *abuf;
static bool romfs_ok;

bool plat_init(const char *title) {
    (void)title;
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();
    gfx3ds_init();
    romfs_ok = R_SUCCEEDED(romfsInit());
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/mutiny", 0777);
    freopen("sdmc:/3ds/mutiny/log.txt", "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
    // Pruebas: sdmc:/3ds/mutiny/test.txt con el mismo guion que MUT_INPUT en Mac
    size_t n = 0;
    void *t = NULL;
    FILE *tf = fopen("sdmc:/3ds/mutiny/test.txt", "rb");
    if (tf) {
        fseek(tf, 0, SEEK_END);
        n = (size_t)ftell(tf);
        fseek(tf, 0, SEEK_SET);
        t = calloc(1, n + 1);
        fread(t, 1, n, tf);
        fclose(tf);
        script_load(t);
        free(t);
        mkdir("sdmc:/3ds/mutiny/shots", 0777);
        fprintf(stderr, "[3ds] guion de prueba cargado (%u bytes)\n", (unsigned)n);
    }
    osSetSpeedupEnable(true);  // New 3DS: 804 MHz
    return true;
}

void plat_shutdown(void) {
    if (audio_ok) {
        ndspChnReset(0);
        ndspExit();
        linearFree(abuf);
    }
    if (romfs_ok) romfsExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

static uint32_t prev_held;
static char shot_name[64];

bool plat_poll(Input *in) {
    if (!aptMainLoop()) return false;
    memset(in, 0, sizeof *in);
    hidScanInput();
    u32 k = hidKeysHeld();
    struct { u32 key; uint32_t b; } map[] = {
        {KEY_A, BTN_A}, {KEY_B, BTN_B}, {KEY_X, BTN_X}, {KEY_Y, BTN_Y}, {KEY_L, BTN_L}, {KEY_R, BTN_R},
        {KEY_START, BTN_START}, {KEY_SELECT, BTN_SELECT}, {KEY_DUP, BTN_UP}, {KEY_DDOWN, BTN_DOWN},
        {KEY_DLEFT, BTN_LEFT}, {KEY_DRIGHT, BTN_RIGHT}, {KEY_ZL, BTN_ZL}, {KEY_ZR, BTN_ZR},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (k & map[i].key) in->held |= map[i].b;
    circlePosition cp;
    hidCircleRead(&cp);
    if (cp.dx * cp.dx + cp.dy * cp.dy > 20 * 20) {
        in->cx = cp.dx / 156.0f;
        in->cy = -cp.dy / 156.0f;
    }
    if (k & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        in->touch = true;
        in->tx = t.px;
        in->ty = t.py;
        in->held |= BTN_TOUCH;
    }
    bool run = true;
    if (script_active()) {
        run = script_apply(in, frame_no, shot_name, sizeof shot_name);
        if (in->touch) in->held |= BTN_TOUCH;
        if (!run) {
            FILE *d = fopen("sdmc:/3ds/mutiny/done.txt", "w");
            if (d) { fputs("ok\n", d); fclose(d); }
            fflush(stderr);
        }
    }
    in->down = in->held & ~prev_held;
    in->up = prev_held & ~in->held;
    prev_held = in->held;
    return run;
}

static void audio_feed(void) {
    if (!audio_ok) return;
    for (int i = 0; i < AUDIO_BUFS; i++) {
        if (wbuf[i].status == NDSP_WBUF_DONE || wbuf[i].status == NDSP_WBUF_FREE) {
            apull(wbuf[i].data_pcm16, AUDIO_FRAMES);
            DSP_FlushDataCache(wbuf[i].data_pcm16, AUDIO_FRAMES * 4);
            ndspChnWaveBufAdd(0, &wbuf[i]);
        }
    }
}

void gfx3ds_capture_next(char *name);

void plat_present(void) {
    if (shot_name[0]) {
        gfx3ds_capture_next(shot_name);
        shot_name[0] = 0;
    }
    gfx3ds_present();
    audio_feed();
    frame_no++;
}

double plat_time(void) { return osGetTime() / 1000.0; }

void plat_sleep(double s) {
    // Mientras se espera al siguiente cuadro de 25 Hz se sigue alimentando el audio
    double end = plat_time() + s;
    while (plat_time() < end) {
        audio_feed();
        double left = end - plat_time();
        if (left <= 0) break;
        svcSleepThread((s64)((left > 0.004 ? 0.004 : left) * 1e9));
    }
}

int plat_frame(void) { return frame_no; }

static void *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    *size = got;
    return buf;
}

void *plat_load_data(const char *name, size_t *size) {
    char p[256];
    // Primero la tarjeta SD (para cambiar el pak sin recompilar), luego el romfs
    snprintf(p, sizeof p, "sdmc:/3ds/mutiny/%s", name);
    void *d = read_file(p, size);
    if (d) return d;
    snprintf(p, sizeof p, "romfs:/%s", name);
    return read_file(p, size);
}

bool plat_save_write(const char *name, const void *data, size_t size) {
    char p[256], tmp[260];
    snprintf(p, sizeof p, "sdmc:/3ds/mutiny/%s", name);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    remove(p);
    return ok && rename(tmp, p) == 0;
}

void *plat_save_read(const char *name, size_t *size) {
    char p[256];
    snprintf(p, sizeof p, "sdmc:/3ds/mutiny/%s", name);
    return read_file(p, size);
}

bool plat_audio_start(int rate, AudioPull pull) {
    (void)rate;
    if (R_FAILED(ndspInit())) return false;  // falta sdmc:/3ds/dspfirm.cdc en consolas sin volcado del DSP
    apull = pull;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, AUDIO_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {1, 1};
    ndspChnSetMix(0, mix);
    abuf = linearAlloc(AUDIO_FRAMES * 4 * AUDIO_BUFS);
    if (!abuf) { ndspExit(); return false; }
    memset(abuf, 0, AUDIO_FRAMES * 4 * AUDIO_BUFS);
    memset(wbuf, 0, sizeof wbuf);
    for (int i = 0; i < AUDIO_BUFS; i++) {
        wbuf[i].data_pcm16 = abuf + i * AUDIO_FRAMES * 2;
        wbuf[i].nsamples = AUDIO_FRAMES;
    }
    audio_ok = true;
    audio_feed();
    return true;
}

int plat_audio_rate(void) { return AUDIO_RATE; }
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}
void plat_screenshot(const char *path) { (void)path; }  // las capturas van por gfx3ds_capture_next
