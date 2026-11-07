// Estados de la aplicación (menús, partida), música, sonidos y partida guardada.
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/audio.h"
#include "../core/mc.h"
#include "../ui/hud.h"
#include "../ui/menus.h"
#include "../ui/ui.h"
#include "game.h"

typedef struct {
    char magic[4];
    uint32_t version;
    uint8_t unlocked[34];
    uint8_t music, sfx;
    int32_t scores[34];
    int32_t won_p1, won_p2;
} Save;

static const Pak *P;
static Save save;
static int state = ST_TITLE;
static int session_score;
static int selected_level = 1, game_type = 1;
static int music_kind;  // 0 ninguna, 1 menú, 2 juego
static int music_voice = -1;
static int fade;        // transición (TransitionTween): >0 oscurece, <0 aclara
static int fade_to = -1;
static int fade_level;
static bool sandbox, fade_sandbox;  // partida de pruebas con todas las armas (START+SELECT)
#define SANDBOX_LEVEL 16            // mapa de batalla 1: ancho, con agua y plataformas
extern void sfx_tick(void);

bool app_sfx_on(void) { return save.sfx; }
bool app_music_on(void) { return save.music; }
int app_total_score(void) { return session_score; }
bool app_level_unlocked(int l) { return l >= 1 && l <= 33 && save.unlocked[l]; }
int app_selected_level(void) { return selected_level; }
int app_wins(int p) { return p == 1 ? save.won_p1 : save.won_p2; }
int app_best_score(int l) { return l >= 1 && l <= 33 ? save.scores[l] : 0; }

static void save_write(void) { plat_save_write("mutiny.sav", &save, sizeof save); }

static void save_load(void) {
    size_t n = 0;
    void *d = plat_save_read("mutiny.sav", &n);
    if (d && n == sizeof save && !memcmp(d, "MUTS", 4)) memcpy(&save, d, sizeof save);
    else {
        memset(&save, 0, sizeof save);
        memcpy(save.magic, "MUTS", 4);
        save.version = 1;
        save.music = save.sfx = 1;
        save.unlocked[1] = 1;
        for (int i = 16; i <= 33; i++) save.unlocked[i] = 1;  // como el frame 1 del original
    }
    free(d);
}

void app_level_won(void) {
    if (selected_level < 33) save.unlocked[selected_level + 1] = 1;
    int sc = world_level_score();
    session_score += sc;
    if (sc > save.scores[selected_level]) save.scores[selected_level] = sc;
    save_write();
}

void app_vs_result(int winner) {
    if (winner == 1) save.won_p1++;
    else if (winner == 2) save.won_p2++;
    save_write();
}

static void music(int kind) {
    if (kind == music_kind && (music_voice >= 0 || !save.music)) return;
    audio_stop_group(AUDIO_MUSIC);
    music_voice = -1;
    music_kind = kind;
    if (!save.music || !kind) return;
    int id = pak_link(P, kind == 1 ? "menu_music" : "game_music");
    music_voice = audio_play(id, true, AUDIO_MUSIC);
}

void app_toggle_music(void) {
    save.music = !save.music;
    int k = music_kind;
    music_kind = -1;
    if (save.music) music(k);
    else { audio_stop_group(AUDIO_MUSIC); music_voice = -1; music_kind = k; }
    save_write();
}

void app_toggle_sfx(void) {
    save.sfx = !save.sfx;
    save_write();
}

static void on_sound(int id, int flags) {
    if (!save.sfx) return;
    if (flags & 1) { audio_stop_sound(id); return; }
    if ((flags & 2) && audio_sound_playing(id)) return;
    audio_play(id, false, AUDIO_SFX);
}

static void on_call(MC *mc, const char *s) {
    world_clip_call(mc, s);
    menus_call(mc, s);
}

static void enter(int st) {
    if (state == ST_GAME && st != ST_GAME) world_unload();
    state = st;
    if (st == ST_GAME) {
        world_give_all = sandbox;
        world_start(selected_level, game_type);
        hud_reset();
        music(2);
    } else {
        menus_enter(st);
        music(st == ST_INTRO ? 0 : 1);  // como el original: la música del menú empieza en el título
    }
}

// Transición: oscurece, cambia de estado y aclara (como TransitionTween)
void app_go(int st, int level, int type) {
    if (fade_to >= 0) return;
    fade_sandbox = st == ST_GAME && (level == 0 || level == selected_level) ? sandbox : false;  // reiniciar la de pruebas la mantiene
    fade_to = st;
    fade_level = level;
    if (type) game_type = type;
    fade = 1;
}

void app_init(const Pak *pak) {
    P = pak;
    srand((unsigned)time(NULL));
    mc_init(pak, on_sound, on_call);
    ui_init();
    save_load();
    menus_init();
    const char *lv = getenv("MUT_LEVEL");
    if (lv) {
        selected_level = atoi(lv);
        game_type = selected_level >= 16 ? 2 : 1;
        if (getenv("MUT_TYPE")) game_type = atoi(getenv("MUT_TYPE"));
        if (getenv("MUT_SEED")) srand((unsigned)atoi(getenv("MUT_SEED")));
        enter(ST_GAME);
    } else {
        enter(getenv("MUT_MENU") ? atoi(getenv("MUT_MENU")) : ST_INTRO);  // pruebas: abrir un menú
    }
}

static void game_tick(const Input *in) {
    static const char *force;
    if (!force) force = getenv("MUT_FORCE_RESULT") ? getenv("MUT_FORCE_RESULT") : "";
    if (*force && plat_frame() == 40) {  // pruebas: mostrar una ventana de fin de nivel
        W.result = atoi(force);
        W.popup = true;
        W.score_gain = (float)world_level_score();
    }
    hud_update(in);
    if (!(W.popup && !W.result)) {  // en pausa el juego se detiene
        mc_tick(W.root);
        world_tick();
    }
    switch (hud_popup_action()) {
    case POP_NEXT:
        W.popup = false;
        app_go(ST_GAME, selected_level + 1, 0);
        break;
    case POP_RESTART:
        W.popup = false;
        session_score = 0;  // RestartLevelButton: _root.score = 0
        app_go(ST_GAME, selected_level, 0);
        break;
    case POP_QUIT: app_go(game_type == 2 ? ST_LEVELS2 : ST_LEVELS1, 0, 0); break;
    case POP_CONGRATS: app_go(ST_CONGRATS, 0, 0); break;
    }
}

void app_tick(const Input *in) {
    if (fade > 0) {
        if (++fade > 8) {
            if (fade_to == ST_GAME && fade_level) selected_level = fade_level;
            sandbox = fade_sandbox;
            if (fade_to == ST_GAME && state == ST_GAME) { world_unload(); state = -1; }
            enter(fade_to);
            fade_to = -1;
            fade = -8;
        }
    } else if (fade < 0) {
        fade++;
    }
    // START+SELECT a la vez (en menús o jugando): partida de pruebas con todas las armas
    bool combo = (in->held & BTN_START) && (in->held & BTN_SELECT) && (in->down & (BTN_START | BTN_SELECT));
    if (combo && !fade && state != ST_INTRO) {
        app_go(ST_GAME, SANDBOX_LEVEL, 2);
        fade_sandbox = true;
        session_score = 0;
        return;
    }
    Input none = *in;
    if (fade) { none.held = none.down = none.up = 0; none.touch = false; }
    if (state == ST_GAME) game_tick(&none);
    else {
        extern int menus_take_request(void);
        int r = menus_update(&none);
        if (!r) r = menus_take_request();
        if (r) {
            int st = r & 0xFF, lvl = (r >> 8) & 0xFF, type = (r >> 16) & 0xFF;
            if (st == ST_GAME) session_score = 0;
            app_go(st, lvl, type);
        }
    }
    sfx_tick();
    ui_tick();
}

static void draw_fade(int screen) {
    if (!fade) return;
    int f = fade > 0 ? fade : -fade;
    uint32_t a = (uint32_t)(f * 255 / 8);
    if (a > 255) a = 255;
    float xy[8] = {0, 0, 400, 0, 400, 240, 0, 240};
    (void)screen;
    gfx_poly(xy, 4, (a << 24) | 0x101018, &MAT_ID);
}

void app_render(void) {
    gfx_begin(SCREEN_TOP);
    if (state == ST_GAME) {
        gfx_clear(0xFF8ACCE8);
        mc_render(W.root, &MAT_ID, &CX_ID);
        hud_render_top();
    } else {
        menus_render_top();
    }
    draw_fade(SCREEN_TOP);
    gfx_end();
    gfx_begin(SCREEN_BOTTOM);
    if (state == ST_GAME) hud_render_bottom();
    else menus_render_bottom();
    draw_fade(SCREEN_BOTTOM);
    gfx_end();
}

void app_shutdown(void) {
    if (state == ST_GAME) world_unload();
    save_write();
}
