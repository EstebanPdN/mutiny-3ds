// Menús. Las dos pantallas son dos ventanas sobre el escenario original de 550x400: arriba la
// escena (cielo, logo, títulos) y abajo, más abajo en el mismo paisaje, los botones táctiles.
#include "menus.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../game/game.h"
#include "ui.h"

extern bool app_music_on(void);
extern bool app_sfx_on(void);
extern void app_toggle_music(void);
extern void app_toggle_sfx(void);
extern bool app_level_unlocked(int l);
extern int app_selected_level(void);
extern int app_wins(int p);

static int st = ST_TITLE;
static int pending;
// Intro de Nitrome (sprite 1917 del SWF): fondo rosa en las dos pantallas, logo solo arriba
#define INTRO_BG 0xFFFFCBFEu
static MC *intro_root, *intro;
static MC *bg;          // menu_background_anim
static MC *sky;         // forma 405
static int hover_level = 1;
static int level2p = 16;
static int frame;
static int help_page;

// Ventanas sobre el escenario de 550x400
#define TOP_OX (-75)
#define TOP_OY (-28)
#define BOT_OX (-115)
#define BOT_OY (-160)  // abajo enseña justo hasta el final del escenario (y = 400)

void menus_init(void) {
    sky = ui_mc_id("menu:sky", 405);
    bg = ui_mc("menu:bg", "menu_background_anim");
}

void menus_enter(int s) {
    st = s;
    frame = 0;
    if (intro) { mc_remove(intro); intro = NULL; }
    if (s == ST_INTRO) {
        if (!intro_root) intro_root = mc_new_root();
        intro = mc_attach_id(intro_root, 1917, "intro", 0);
    }
    if (s == ST_LEVELS1) {
        hover_level = app_selected_level() >= 1 && app_selected_level() <= 15 ? app_selected_level() : 1;
    }
    if (s == ST_LEVELS2 && app_selected_level() >= 16) level2p = app_selected_level();
    help_page = 0;
}

void menus_call(MC *mc, const char *s) {
    (void)mc;
    // Último cuadro de la intro: _root.tt.doTween("pre_title_screen")
    if (st == ST_INTRO && strstr(s, "doTween")) pending = MENU_GO(ST_TITLE, 0, 0);
}

static void bg_tick(void) {
    MC *w = mc_get(bg, "water"), *b = mc_get(bg, "bg");
    if (w) {
        float x = mc_x(w) - 8;
        mc_set_x(w, x <= -64 ? 0 : x);
    }
    struct { const char *n; float sp, wrap; } L[] = {{"cloudBase", 2, 550}, {"frontClouds", 4, 1000}, {"hills", 2, 840}, {"backClouds", 1, 900}};
    for (int i = 0; i < 4; i++) {
        MC *m = mc_get(b, L[i].n);
        if (!m) continue;
        float x = mc_x(m) - L[i].sp;
        mc_set_x(m, x <= -L[i].wrap ? 0 : x);
    }
}

static void draw_bg(float ox, float oy, int w, int h) {
    ui_draw(sky, ox, oy);
    ui_draw(bg, ox, oy - 5);
    // Por debajo del escenario original: agua profunda
    float bottom = oy + 400;
    if (bottom < h) ui_rect(0, bottom, (float)w, h - bottom, 0xFF2F68B4);
}

// Botón de texto original (play_button, back_button...): estados up/over y área de toque
static bool text_button(const char *link, float x, float y) {
    MC *m = ui_mc(link, link);
    if (!m) return false;
    float b[4];
    mc_goto_label(m, "up", false);
    mc_bounds(m, b);
    float bx = x + b[0] - m->m.tx - 6, by = y + b[1] - m->m.ty - 4, bw = b[2] - b[0] + 12, bh = b[3] - b[1] + 8;
    bool hov = ui_hover(bx, by, bw, bh);
    mc_goto_label(m, hov ? "over" : "up", false);
    ui_draw(m, x, y);
    if (ui_tap(bx, by, bw, bh)) {
        sfx("click");
        return true;
    }
    return false;
}

static bool toggles(void) {
    MC *mu = ui_mc("menu:music", "music toggle button"), *fx = ui_mc("menu:sfx", "sfx toggle button");
    bool hm = ui_hover(278, 2, 20, 20), hf = ui_hover(299, 2, 20, 20);
    mc_goto_label(mu, app_music_on() ? (hm ? "_on_over" : "_on_up") : (hm ? "_off_over" : "_off_up"), false);
    mc_goto_label(fx, app_sfx_on() ? (hf ? "_on_over" : "_on_up") : (hf ? "_off_over" : "_off_up"), false);
    ui_draw(mu, 288, 12);
    ui_draw(fx, 309, 12);
    if (ui_tap(278, 2, 20, 20)) { app_toggle_music(); return true; }
    if (ui_tap(299, 2, 20, 20)) { app_toggle_sfx(); sfx("click"); return true; }
    return false;
}

static void panel_top(const char *title) {
    ui_nine(411, 6, 20, 10, TOP_W - 40, TOP_H - 20, NULL);
    if (title) ui_text("mn:ptitle", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, 20, title);
}

// ------------------------------------------------------------ actualización
static int req;

static void title_bottom(void) {
    if (text_button("play_button", BOT_W / 2, 62)) req = MENU_GO(ST_GAMESELECT, 0, 0);
    if (text_button("scores_button", BOT_W / 2, 102)) req = MENU_GO(ST_SCORES, 0, 0);
    if (text_button("help_button", BOT_W / 2, 142)) req = MENU_GO(ST_HELP, 0, 0);
    if (text_button("credits_button", BOT_W / 2, 182)) req = MENU_GO(ST_CREDITS, 0, 0);
    ui_text("mn:copy", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, 222, "nitrome ltd 2008 - 3ds port");
}

static bool back_button(float y) { return text_button("back_button", BOT_W / 2, y) || (ui_in()->down & BTN_B); }

static void gameselect_bottom(void) {
    ui_nine(411, 6, 10, 30, BOT_W - 20, 150, NULL);
    if (text_button("1p_button", BOT_W / 2, 70) || (ui_in()->down & BTN_L)) req = MENU_GO(ST_LEVELS1, 0, 1);
    if (text_button("2p_button", BOT_W / 2, 126) || (ui_in()->down & BTN_R)) req = MENU_GO(ST_LEVELS2, 0, 2);
    if (back_button(206)) req = MENU_GO(ST_TITLE, 0, 0);
}

#define LS_X0 7
#define LS_DX 62
#define LS_Y0 3
#define LS_DY 78

static void levels1_bottom(void) {
    const Input *in = ui_in();
    for (int i = 1; i <= 15; i++) {
        int c = (i - 1) % 5, r = (i - 1) / 5;
        float x = LS_X0 + c * LS_DX, y = LS_Y0 + r * LS_DY;
        char key[16];
        snprintf(key, sizeof key, "ls_%d", i);
        MC *m = ui_mc(key, "level_select_button");
        bool open = app_level_unlocked(i);
        bool hov = ui_hover(x, y, 51, 77) && open;
        if (hov) hover_level = i;
        mc_goto_label(m, hov || i == hover_level ? "over" : "up", false);
        MC *pc = mc_get(m, "pirate_clip"), *q = mc_get(m, "question_mark"), *num = mc_get(m, "level_number");
        if (pc) {
            mc_goto(pc, i, false);
            if (!open) { CX black = {0, 0, 0, 1, 0, 0, 0, 0}; mc_set_cx(pc, &black); }
            else mc_set_cx(pc, &CX_ID);
        }
        mc_set_visible(q, !open);
        char n[8];
        snprintf(n, sizeof n, "%02d", i);
        mc_set_text(num, open ? n : "");
        ui_draw(m, x, y);
        if (open && ui_tap(x, y, 51, 77)) {
            sfx("click");
            req = MENU_GO(ST_GAME, i, 1);
        }
    }
    int d = (in->down & BTN_RIGHT) ? 1 : (in->down & BTN_LEFT) ? -1 : (in->down & BTN_DOWN) ? 5 : (in->down & BTN_UP) ? -5 : 0;
    if (d) {
        int n = hover_level + d;
        if (n >= 1 && n <= 15 && app_level_unlocked(n)) hover_level = n;
    }
    if (in->down & BTN_A) req = MENU_GO(ST_GAME, hover_level, 1);
    if (in->down & BTN_B) req = MENU_GO(ST_GAMESELECT, 0, 0);
}

static void levels2_bottom(void) {
    const Input *in = ui_in();
    ui_nine(411, 6, 6, 4, BOT_W - 12, 168, NULL);
    MC *w = ui_mc("menu:ls2p", "level_select_2p");
    MC *content = mc_get(w, "content_clip"), *prev = mc_get(w, "prev_button"), *next = mc_get(w, "next_button"), *play = mc_get(w, "play_button");
    if (content) mc_goto(content, level2p - 15, false);
    mc_set_visible(prev, level2p > 16);
    mc_set_visible(next, level2p < 33);
    float cx = BOT_W / 2, cy = 82;
    bool hp = ui_hover(cx - 140 - 16, cy - 20, 32, 40), hn = ui_hover(cx + 137 - 16, cy - 20, 32, 40), hy = ui_hover(cx - 50, cy + 80, 100, 30);
    mc_goto_label(prev, hp ? "over" : "up", false);
    mc_goto_label(next, hn ? "over" : "up", false);
    mc_goto_label(play, hy ? "over" : "up", false);
    ui_draw(w, cx, cy);
    if ((ui_tap(cx - 140 - 16, cy - 20, 32, 40) || (in->down & (BTN_LEFT | BTN_L))) && level2p > 16) { level2p--; sfx("click"); }
    if ((ui_tap(cx + 137 - 16, cy - 20, 32, 40) || (in->down & (BTN_RIGHT | BTN_R))) && level2p < 33) { level2p++; sfx("click"); }
    if (ui_tap(cx - 50, cy + 80, 100, 30) || (in->down & BTN_A)) { sfx("click"); req = MENU_GO(ST_GAME, level2p, 2); }
    if (back_button(214)) req = MENU_GO(ST_GAMESELECT, 0, 0);
}

int menus_update(const Input *in) {
    ui_input(in);
    frame++;
    req = 0;
    if (st == ST_INTRO) {
        if (intro_root) mc_tick(intro_root);
        // Se puede saltar tocando la pantalla o con A/START
        if (frame > 12 && (ui_touch_began_any() || (in->down & (BTN_A | BTN_START)))) req = MENU_GO(ST_TITLE, 0, 0);
        return req;
    }
    bg_tick();
    return req;
}

static void intro_top(void) {
    gfx_clear(INTRO_BG);
    if (!intro) return;
    Mat m = mat_translate(TOP_W / 2, TOP_H / 2);
    mc_render(intro, &m, &CX_ID);
}

// Abajo: el mismo fondo y solo las bombas de cereza, que suben desde la pantalla táctil
// hasta el logo como si las dos pantallas fueran una (la de abajo empieza donde acaba la de arriba)
static void intro_bottom(void) {
    gfx_clear(INTRO_BG);
    if (!intro) return;
    int bomb = pak_link(mc_pak(), "cherryBomb");
    Mat base = mat_translate(BOT_W / 2, TOP_H / 2 - TOP_H);
    for (MC *c = intro->child; c; c = c->next)
        if (c->ch == bomb) mc_render(c, &base, &CX_ID);
}

// El dibujo de la pantalla de abajo también resuelve los toques (interfaz inmediata)

void menus_render_top(void) {
    if (st == ST_INTRO) {
        intro_top();
        return;
    }
    gfx_clear(0xFF8ACCE8);
    draw_bg(TOP_OX, TOP_OY, TOP_W, TOP_H);
    char buf[96];
    switch (st) {
    case ST_TITLE: {
        ui_image("logo", (TOP_W - 316) / 2, 46);  // 316x108
        break;
    }
    case ST_GAMESELECT:
        panel_top("select game");
        ui_text("mn:gs1", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 64, "1 player: fight your way through|15 levels against the computer.");
        ui_text("mn:gs2", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 124, "2 players: take turns on the|same 3ds, passing it between|you. 18 battle maps.");
        break;
    case ST_LEVELS1: {
        panel_top("select level");
        MC *m = ui_mc("menu:bigpirate", "level_select_button");
        mc_goto_label(m, "up", false);
        MC *pc = mc_get(m, "pirate_clip");
        if (pc) {
            mc_goto(pc, hover_level, false);
            Mat m2 = {3, 0, 0, 3, TOP_W / 2 - 3 * pc->m.tx, 96 - 3 * pc->m.ty};  // x3 entero: sigue siendo pixel-perfect
            ui_draw_m(pc, &m2, NULL);
        }
        snprintf(buf, sizeof buf, "level %d", hover_level);
        ui_text("mn:lvl", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, 150, buf);
        extern int app_best_score(int l);
        int best = app_best_score(hover_level);
        snprintf(buf, sizeof buf, best ? "best score %d" : "not beaten yet", best);
        ui_text("mn:best", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 172, buf);
        ui_text("mn:lhint", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 200, "tap a level to play   b back");
        break;
    }
    case ST_LEVELS2: {
        // Bloque centrado en vertical dentro del panel (10..230)
        panel_top(NULL);
        float y0 = 10 + (220 - 172) / 2.0f;
        ui_text("mn:2title", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, y0, "2 player game");
        snprintf(buf, sizeof buf, "red wins %d    blue wins %d", app_wins(1), app_wins(2));
        ui_text("mn:wins", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, y0 + 40, buf);
        ui_text("mn:maplbl", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, y0 + 72, "battle map");
        snprintf(buf, sizeof buf, "%d of 18", level2p - 15);
        ui_text("mn:map", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, y0 + 102, buf);
        ui_text("mn:2hint", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, y0 + 134, "pass the 3ds to your rival|after each turn.");
        break;
    }
    case ST_HELP: {
        panel_top("help");
        MC *h = ui_mc_id("menu:help", 1947);
        // El texto original habla del ratón: se oculta y se pone el de la 3DS
        for (MC *c = h->child; c; c = c->next)
            if (c->is_text) mc_set_visible(c, false);
        ui_draw(h, TOP_W / 2, 136);
        ui_text("mn:help", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 176,
                h->frame < 65 ? "use your weapons to fire at your|opponents. touch, pull back and|let go to throw them."
                              : "choose a character and throw|him to move him around.");
        break;
    }
    case ST_CREDITS:
        panel_top("credits");
        ui_text("mn:cr0", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 46, "game by nitrome  www.nitrome.com");
        ui_text("mn:cr1", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, 64, "programming");
        ui_text("mn:cr2", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 92, "chris burt  brown|heather stancliffe");
        ui_text("mn:cr3", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, 122, "artwork");
        ui_text("mn:cr4", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 150, "jon annal|mat annal");
        ui_text("mn:cr5", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, 178, "music");
        ui_text("mn:cr6", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, 206, "dave cowen");
        break;
    case ST_SCORES: {
        panel_top("best scores");
        extern int app_best_score(int l);
        int total = 0;
        for (int i = 1; i <= 15; i++) {
            int c = (i - 1) / 8, r = (i - 1) % 8;
            int s = app_best_score(i);
            total += s;
            snprintf(buf, sizeof buf, "level %02d   %d", i, s);
            char k[16];
            snprintf(k, sizeof k, "mn:sc%d", i);
            ui_text(k, FONT_DANGLE, ALIGN_LEFT, 60 + c * 150, 50 + r * 18, buf);
        }
        snprintf(buf, sizeof buf, "%d", total);
        float lw = ui_text_width("mn:sctotw", FONT_PIRATE, "total");
        ui_text("mn:sctotl", FONT_PIRATE, ALIGN_LEFT, TOP_W / 2 - (lw + 50) / 2, 194, "total");
        ui_text("mn:sctot", FONT_DANGLE, ALIGN_LEFT, TOP_W / 2 - (lw + 50) / 2 + lw + 14, 200, buf);
        break;
    }
    case ST_CONGRATS: {
        MC *c = ui_mc("menu:congrats", "congrats_background_anim");
        ui_draw(c, TOP_OX, TOP_OY);
        ui_text("mn:cg", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, 30, "congratulations!");
        break;
    }
    }
}

void menus_render_bottom(void) {
    if (st == ST_INTRO) {
        intro_bottom();
        return;
    }
    gfx_clear(0xFF2F68B4);
    draw_bg(BOT_OX, BOT_OY, BOT_W, BOT_H);
    req = 0;
    switch (st) {
    case ST_TITLE: title_bottom(); break;
    case ST_GAMESELECT: gameselect_bottom(); break;
    case ST_LEVELS1: levels1_bottom(); break;
    case ST_LEVELS2: levels2_bottom(); break;
    case ST_CREDITS:
        ui_text("mn:port", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, 150, "nintendo 3ds port");
        if (back_button(200)) req = MENU_GO(ST_TITLE, 0, 0);
        break;
    case ST_HELP:
    case ST_SCORES:
        if (back_button(200)) req = MENU_GO(ST_TITLE, 0, 0);
        break;
    case ST_CONGRATS:
        if (text_button("back to title (congrats)", BOT_W / 2, 200) || (ui_in()->down & (BTN_A | BTN_B))) req = MENU_GO(ST_TITLE, 0, 0);
        break;
    }
    if (st != ST_LEVELS1) toggles();
    if (req) pending = req;
}

int menus_take_request(void) {
    int r = pending;
    pending = 0;
    return r;
}
