// Partida: arriba el juego (cámara original adaptada a 400x240), abajo los controles táctiles.
//
// La pantalla de abajo tiene siempre la misma estructura sobre una cubierta de barco:
//   cabecera  - vida de los dos equipos (barras originales) y botón de menú
//   minimapa  - el nivel con sus colores, los piratas, los cofres y la vista de la cámara
//   contexto  - cambia con el turno: elegir pirata, elegir arma, apuntar, colocar, acción...
// Para lanzar se toca en cualquier sitio bajo la cabecera, se estira hacia atrás y se suelta.
// El ratón del original se emula en coordenadas del mundo (W.mouse_x/y, W.mouse_down).
#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game/game.h"
#include "../game/lines.h"
#include "ui.h"

enum { M_WAIT, M_CHOOSE, M_WEAPONS, M_AIM, M_TARGET, M_ACTION, M_CANNON, M_VOODOO, M_POPUP, M_SPEECH };

#define HDR_H 38
#define MAP_X 4
#define MAP_Y 39
#define MAP_W 312
#define CTX_X 4
#define CTX_W 312
// El minimapa crece cuando la zona de contexto necesita poco sitio (esperar, colocar)
static int map_h = 78, ctx_y = 120, ctx_h = 117;
#define MAP_H map_h
#define CTX_Y ctx_y
#define CTX_H ctx_h
#define PULL_R 84

static int mode, prev_mode;
static int popup_action;
static bool paused, popup_fresh;
// Ventana de fin de nivel: al aparecer no acepta nada durante popup_guard frames y exige un toque
// nuevo (el toque que acelera el discurso de victoria no debe pulsar "next level")
static int popup_guard;
static bool popup_armed;
static bool fan_on;
static int fan_side;
static bool aim_on;
static float pull_x, pull_y;
static int aim_sx, aim_sy;
static Solid *aim_obj;
static float cur_x, cur_y;
static bool cur_valid;
static bool cannon_move;
static bool pad_on;
static float pad_wx, pad_wy;
static int hover_slot = -1;
static bool weapon_drag;  // arrastrando por la rejilla de armas
static int choose_idx;
static bool card_drag;  // arrastrando el dedo por las tarjetas de piratas
static float team_frame[2] = {96, 96};

extern void voodoo_set_target(Weapon *w, Character *c);

static const char *const desc3ds[][3] = {
    {"cherryBomb", "cherry bomb", "Basic weak weapon which|explodes on impact. Touch the|screen, pull back and let go|to aim and set the power."},
    {"dynamite", "dynamite", "This weapon explodes when it|comes to rest. Touch, pull back|and let go to aim and set|the power."},
    {"boulder", "boulder", "Large boulder which can bash|other players out of the way.|Touch, pull back and let go|to throw it."},
    {"piecesOfEight", "pieces of eight", "Eight coins which inflict a|small amount of damage. You|get eight throws with this|weapon."},
    {"rumBottle", "rum bottle", "This weapon explodes on impact|and sets the nearby area on|fire. Touch, pull back and|let go to throw it."},
    {"banana", "banana", "This weapon is very bouncy.|Pull back and let go, then|tap boom or press a to make|it explode."},
    {"parachuteBomb", "parachute bomb", "Pull back and let go to throw|it. As it drifts down hold|the fan buttons to push it|left or right."},
    {"woodenCrate", "crates", "Tap the map or move the cursor|with the d-pad and press place|to put down three crates. Use|them to protect your crew."},
    {"gunpowderBarrel", "gunpowder barrels", "Tap the map or move the cursor|and press place to put down|two barrels. These explode|when hit by a weapon."},
    {"seagull", "seagull", "Choose a height on the map for|the seagull to fly at, then|tap poop again and again to|poop on the enemy!"},
    {"mine", "mine", "Pull back and let go to throw|the mine. It will detonate|when another player moves|nearby."},
    {"cannon", "cannon", "Drag the cannon into position|within the circle. Then pull|back to turn it and let go|to fire!"},
    {"anchor", "anchor", "Choose a spot on the map and|tap drop to send a huge|anchor down onto enemies!"},
    {"voodooDoll", "voodoo doll", "Choose an enemy player, then|pull back to throw the doll.|The enemy will fly off in the|same direction!"},
    {"tidalWave", "tidal wave", "Tap go to send a huge tidal|wave across the bottom of the|stage. It will affect all|players it hits."},
    {"throw", "throw character", "Touch, pull back and let go|to throw your character. You|get to use this once per turn|before you use a weapon."},
    {"endTurn", "end go", "Tap here if you want to finish|your turn without using a|weapon."},
    {"close", "close", "Cancel and select another|player."},
};

static const char *desc_of(const char *w, int field) {
    for (size_t i = 0; i < sizeof desc3ds / sizeof desc3ds[0]; i++)
        if (!strcmp(desc3ds[i][0], w)) return desc3ds[i][field];
    return "";
}

const char *weapon_title(WType w) {
    for (size_t i = 0; i < sizeof HOVER_TEXT / sizeof HOVER_TEXT[0]; i++)
        if (!strcmp(HOVER_TEXT[i].weapon, weapon_names[w])) return HOVER_TEXT[i].title;
    return weapon_names[w];
}

void hud_fan(bool on) { fan_on = on; }

void hud_pause(void) {
    if (W.popup || W.result) return;
    paused = true;
    popup_fresh = true;
    W.popup = true;
}

void hud_reset(void) {
    mode = prev_mode = M_WAIT;
    popup_action = POP_NONE;
    paused = false;
    aim_on = pad_on = false;
    aim_obj = NULL;
    cannon_move = false;
    hover_slot = -1;
    weapon_drag = false;
    team_frame[0] = team_frame[1] = 96;
}

int hud_popup_action(void) {
    int a = popup_action;
    popup_action = POP_NONE;
    return a;
}

static Character *sel(void) { return W.current ? W.current->selected : NULL; }
static int team_no(void) { return W.current ? W.current->number : 1; }
static uint32_t team_col(int n) { return n == 2 ? COL_BLUE : COL_RED; }
static const char *btn_style(void) { return team_no() == 2 ? "btn_blue" : "btn_red"; }

static int compute_mode(void) {
    if (W.popup || paused) return M_POPUP;
    if (W.speech) return M_SPEECH;
    if (W.result) return M_WAIT;
    Team *t = W.current;
    Character *s = t->selected;
    if (t->ai) return M_WAIT;
    if (!s) return M_CHOOSE;
    if (!s->weapon_selected) return M_WEAPONS;
    Weapon *w = s->weapon;
    if (!w) return s->thrown ? M_WAIT : M_AIM;
    switch (w->type) {
    case W_VOODOODOLL: return w->fired ? M_WAIT : (w->target ? M_AIM : M_VOODOO);
    case W_WOODENCRATE:
    case W_GUNPOWDERBARREL: return w->finished ? M_WAIT : M_TARGET;
    case W_ANCHOR: return w->fired ? M_WAIT : M_TARGET;
    case W_SEAGULL: return !w->fired ? M_TARGET : (w->finished ? M_WAIT : M_ACTION);
    case W_TIDALWAVE: return w->fired ? M_WAIT : M_ACTION;
    case W_CANNON: return w->fired ? M_WAIT : M_CANNON;
    case W_BANANA:
    case W_PARACHUTEBOMB: return !w->fired ? M_AIM : (w->finished ? M_WAIT : M_ACTION);
    default: return w->fired ? M_WAIT : M_AIM;
    }
}

void hud_scroll_input(int *dx, int *dy) {
    const Input *in = ui_in();
    *dx = *dy = 0;
    if (mode == M_TARGET || mode == M_POPUP) return;
    uint32_t h = in->held;
    if ((h & BTN_LEFT) || in->cx < -0.4f) *dx -= 1;
    if ((h & BTN_RIGHT) || in->cx > 0.4f) *dx += 1;
    if ((h & BTN_UP) || in->cy < -0.4f) *dy -= 1;
    if ((h & BTN_DOWN) || in->cy > 0.4f) *dy += 1;
}

// ------------------------------------------------------------ paneles y botones
static void panel(float x, float y, float w, float h) {
    int blue = pak_link(mc_pak(), "ui_popup_blue");
    ui_nine(team_no() == 2 && blue > 0 ? blue : 411, 6, x, y, w, h, NULL);
}

static bool button(const char *key, const char *style, float x, float y, float w, float h, const char *label, int font) {
    bool hov = ui_hover(x, y, w, h);
    char img[48];
    snprintf(img, sizeof img, "%s%s", style, w <= 44 ? "_tiny" : w <= 60 ? "_small" : (w >= 140 ? "_big" : ""));
    CX dim = {0.8f, 0.8f, 0.8f, 1, 0, 0, 0, 0};
    ui_image_cx(img, x, y + (hov ? 1 : 0), hov ? &dim : NULL);
    if (label) {
        char k[64];
        snprintf(k, sizeof k, "btn:%s", key);
        ui_text(k, font, ALIGN_CENTER, floorf(x + w / 2), floorf(y + h / 2 - (font == FONT_PIRATE ? 6 : 6)) + (hov ? 1 : 0), label);
    }
    return ui_tap(x, y, w, h);
}

// ------------------------------------------------------------ colores del minimapa (media de cada tile, del pak)
static uint32_t tile_color(int ch) {
    uint32_t c = pak_tile_color(mc_pak(), ch);
    return c ? c : 0xFF8090A0;
}

// ------------------------------------------------------------ minimapa
typedef struct { float x, y, dot; } MapGeom;

static MapGeom map_geom(void) {
    MapGeom g;
    float d = floorf(fminf((MAP_W - 10) / (float)W.lw, (MAP_H - 10) / (float)W.lh));
    if (d < 1) d = 1;
    g.dot = d;
    g.x = floorf(MAP_X + (MAP_W - d * W.lw) / 2);
    g.y = floorf(MAP_Y + (MAP_H - d * W.lh) / 2);
    return g;
}

static void map_draw(bool cursor) {
    panel(MAP_X, MAP_Y, MAP_W, MAP_H);
    MapGeom g = map_geom();
    ui_rect(g.x, g.y, g.dot * W.lw, g.dot * W.lh, 0xFF1E2A44);
    int wl = W.has_water ? (int)(W.water_y / 32) : W.lh;
    if (wl < W.lh) {
        ui_rect(g.x, g.y + wl * g.dot, g.dot * W.lw, g.dot * (W.lh - wl), 0xFF2C58A0);
        ui_rect(g.x, g.y + wl * g.dot, g.dot * W.lw, 1, 0xFF7CB4F0);
    }
    for (int y = 0; y < W.lh; y++)
        for (int x = 0; x < W.lw; x++) {
            uint16_t t = W.grid[y * W.lw + x];
            if (!t || t == 0xFFFF) continue;
            uint32_t c = t == 0xFFFE ? 0xFF8090A0 : tile_color(t);
            ui_rect(g.x + x * g.dot, g.y + y * g.dot, g.dot, g.dot, c);
        }
    float k = g.dot / 32.0f;
    float vx = floorf(g.x + W.cam_x * k), vy = floorf(g.y + W.cam_y * k), vw = floorf(VIEW_W * k), vh = floorf(VIEW_H * k);
    ui_frame(vx, vy, vw, vh, 0xA0F8F8F8);
    for (int i = 0; i < W.nchests; i++) {
        Chest *c = W.chests[i];
        if (c->visibility <= 0) continue;
        float px = floorf(g.x + c->clip.x * k), py = floorf(g.y + c->clip.y * k);
        ui_rect(px - 2, py - 2, 4, 4, 0xFF2A2222);
        ui_rect(px - 1, py - 1, 2, 2, 0xFFFFE020);
    }
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < W.teams[t].n; i++) {
            Character *c = W.teams[t].chars[i];
            if (!c->alive && c->map_visibility <= 0) continue;
            float px = floorf(g.x + c->s.clip.x * k), py = floorf(g.y + c->s.clip.y * k);
            uint32_t col = t ? 0xFF3360FF : 0xFFFF3A29;
            uint32_t a = (uint32_t)(255 * fmaxf(0, fminf(1, c->map_visibility)));
            bool me = c == sel() || (mode == M_CHOOSE && W.current->chars[choose_idx] == c);
            if (me && card_drag && (plat_frame() / 3) % 2) continue;  // parpadea mientras se elige
            ui_rect(px - 3, py - 3, 6, 6, (a << 24) | (me ? 0xF8F8F8 : 0x2A2222));
            ui_rect(px - 2, py - 2, 4, 4, (a << 24) | (col & 0xFFFFFF));
        }
    if (cursor) {
        float px = floorf(g.x + cur_x * k), py = floorf(g.y + cur_y * k);
        uint32_t cc = cur_valid ? 0xFFF8F8F8 : 0xFFFF4040;
        ui_rect(px - 6, py, 4, 1, cc);
        ui_rect(px + 3, py, 4, 1, cc);
        ui_rect(px, py - 6, 1, 4, cc);
        ui_rect(px, py + 3, 1, 4, cc);
    }
}

static bool map_touch(float *wx, float *wy) {
    if (!ui_touching()) return false;
    if (!(ui_start_x() >= MAP_X && ui_start_x() < MAP_X + MAP_W && ui_start_y() >= MAP_Y && ui_start_y() < MAP_Y + MAP_H)) return false;
    MapGeom g = map_geom();
    *wx = (ui_tx() - g.x) / g.dot * 32;
    *wy = (ui_ty() - g.y) / g.dot * 32;
    return true;
}

// ------------------------------------------------------------ cabecera
static void team_bars_tick(void) {
    for (int t = 0; t < 2; t++) {
        float h = 0, m = 0;
        for (int i = 0; i < W.teams[t].n; i++) {
            h += W.teams[t].chars[i]->health;
            m += W.teams[t].chars[i]->max_health;
        }
        float target = 1 + floorf(96 * (m > 0 ? h / m : 0));
        if (team_frame[t] < target) team_frame[t] = fminf(team_frame[t] + 1, target);
        else if (team_frame[t] > target) team_frame[t] = fmaxf(team_frame[t] - 1, target);
    }
}

static void header_draw(bool menu) {
    MC *t1 = ui_mc_id("team1", 1999), *t2 = ui_mc_id("team2", 2006);
    mc_goto(t1, (int)team_frame[0], false);
    mc_goto(t2, (int)team_frame[1], false);
    MC *op = mc_get(t2, "opponent_image");
    if (op) mc_goto(op, W.level, false);
    // Turno: la barra del equipo que no juega se oscurece un poco (sin marcos)
    int cur = W.current ? W.current->number : 1;
    CX dim = {0.55f, 0.55f, 0.55f, 1, 0, 0, 0, 0};
    ui_draw_cx(t1, 4 + 66, 20, cur == 1 ? NULL : &dim);
    ui_draw_cx(t2, BOT_W - 4 - 66, 20, cur == 2 ? NULL : &dim);
    // Hueco entre las dos barras: de x=136 a x=184
    if (menu) button("hd:menu", "btn_gray", BOT_W / 2 - 20, 15, 40, 20, "menu", FONT_DANGLE);
}

static bool header_menu_tap(void) { return ui_tap(BOT_W / 2 - 22, 9, 44, 29); }

// ------------------------------------------------------------ tarjetas de piratas
static void card_geom(int n, float *x0, float *cw) {
    *cw = fminf(50, (CTX_W - 12) / (float)n);
    *x0 = floorf(CTX_X + (CTX_W - *cw * n) / 2);
}

static void card_draw(Character *c, float x, float y, float w, float h, bool hl) {
    uint32_t col = team_col(c->team->number);
    ui_rect(x, y, w, h, COL_OUTLINE);
    ui_rect(x + 1, y + 1, w - 2, h - 2, hl ? 0xFF55504C : 0xFF3C3836);
    ui_rect(x + 1, y + 1, w - 2, 2, hl ? COL_WHITE : col);
    MC *m = ui_mc(c->s.clip.link, c->s.clip.link);
    mc_goto(m, 1, false);
    float bb[4];
    float sw = mc_bounds(m, bb) ? bb[2] - bb[0] : 20;
    float sc = sw * 2 <= w - 4 ? 2 : 1;  // escala entera: sigue siendo pixel-perfect
    Mat mm = {sc, 0, 0, sc, floorf(x + w / 2), floorf(y + h / 2 - 6)};
    CX gray = {0.25f, 0.25f, 0.25f, 1, 30, 30, 30, 0};
    ui_draw_m(m, &mm, c->alive ? NULL : &gray);
    float bw = w - 8, f = c->alive ? c->shown_health / c->max_health : 0;
    ui_rect(x + 4, y + h - 10, bw, 6, COL_OUTLINE);
    if (f > 0) ui_rect(x + 5, y + h - 9, (bw - 2) * f, 4, f > 0.5f ? 0xFF40D040 : f > 0.25f ? 0xFFF0C020 : 0xFFE63013);
    if (!c->alive) {
        ui_line(x + 6, y + 8, x + w - 6, y + h - 16, 2, 0xC0E63013);
        ui_line(x + w - 6, y + 8, x + 6, y + h - 16, 2, 0xC0E63013);
    }
}

static int cards_pick(Team *t, int *idx) {
    const Input *in = ui_in();
    if (*idx >= t->n || *idx < 0 || !t->chars[*idx]->alive) {
        for (int i = 0; i < t->n; i++)
            if (t->chars[i]->alive) { *idx = i; break; }
    }
    int dir = (in->down & (BTN_R | BTN_RIGHT)) ? 1 : (in->down & (BTN_L | BTN_LEFT)) ? -1 : 0;
    if (dir)
        for (int k = 1; k <= t->n; k++) {
            int i = ((*idx + dir * k) % t->n + t->n) % t->n;
            if (t->chars[i]->alive) { *idx = i; break; }
        }
    // Mantener pulsado y arrastrar por las tarjetas cambia el pirata (parpadea en el mapa);
    // al soltar sobre una tarjeta se elige, al soltar fuera no se elige ninguno.
    float x0, cw;
    card_geom(t->n, &x0, &cw);
    float cy = CTX_Y + 6, ch = 84;
    if (ui_touch_began(x0, cy, cw * t->n, ch)) card_drag = true;
    if (card_drag) {
        int tx = ui_tx(), ty = ui_ty();
        bool inside = tx >= x0 && tx < x0 + cw * t->n && ty >= cy && ty < cy + ch;
        int i = inside ? (int)((tx - x0) / cw) : -1;
        if (ui_touching()) {
            if (i >= 0 && i < t->n && t->chars[i]->alive && i != *idx) { *idx = i; return 1; }
            return 0;
        }
        card_drag = false;
        if (i >= 0 && i < t->n && t->chars[i]->alive) { *idx = i; return 2; }
        return 0;
    }
    if (in->down & BTN_A) return 2;
    return dir ? 1 : 0;
}

static void cards_draw(Team *t, int idx, const char *hint) {
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    float x0, cw;
    card_geom(t->n, &x0, &cw);
    for (int i = 0; i < t->n; i++) {
        bool hl = i == idx;
        card_draw(t->chars[i], x0 + i * cw + 1, CTX_Y + 6, cw - 2, 84, hl);
    }
    ui_text("cd:hint", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, CTX_Y + 95, hint);
}

// ------------------------------------------------------------ rejilla de armas (iconos originales)
static const char *const slots[18] = {
    "throw", "cherryBomb", "boulder", "dynamite", "piecesOfEight", "rumBottle", "banana", "parachuteBomb", "woodenCrate",
    "gunpowderBarrel", "seagull", "mine", "cannon", "anchor", "voodooDoll", "tidalWave", "endTurn", "close",
};
#define SLOT_DX 31
#define SLOT_W 29
#define SLOT_H 40
#define GRID_X (CTX_X + (CTX_W - (8 * SLOT_DX + SLOT_W)) / 2)
#define GRID_Y (CTX_Y + 22)

static void slot_rect(int i, float *x, float *y) {
    *x = GRID_X + (i % 9) * SLOT_DX;
    *y = GRID_Y + (i / 9) * (SLOT_H + 4);
}

static bool slot_disabled(Character *s, const char *w) {
    if (!strcmp(w, "throw")) return !s->can_throw;
    if (!strcmp(w, "endTurn")) return false;
    if (!strcmp(w, "close")) return !s->can_throw;
    return character_count_weapon(s, weapon_from_name(w)) == 0;
}

// Casilla bajo el dedo (o -1)
static int slot_at(int tx, int ty) {
    for (int i = 0; i < 18; i++) {
        float x, y;
        slot_rect(i, &x, &y);
        if (tx >= x && tx < x + SLOT_W && ty >= y && ty < y + SLOT_H) return i;
    }
    return -1;
}

static void weapons_update(void) {
    Character *s = sel();
    hover_slot = -1;
    if ((ui_in()->down & BTN_B) && s->can_throw) {
        weapon_drag = false;
        character_unequip(s);
        W.current->selected = NULL;
        return;
    }
    // Como con los piratas: mantener y deslizar por la rejilla resalta el arma bajo el dedo y
    // muestra su información arriba; al soltar sobre un arma se elige, fuera de la rejilla no.
    float gx, gy;
    slot_rect(0, &gx, &gy);
    if (ui_touch_began(gx - 2, gy - 2, 8 * SLOT_DX + SLOT_W + 4, 2 * SLOT_H + 8)) weapon_drag = true;
    if (!weapon_drag) return;
    int i = slot_at(ui_tx(), ui_ty());
    if (ui_touching()) {
        hover_slot = i;
        return;
    }
    weapon_drag = false;
    if (i >= 0) {
        const char *w = slots[i];
        if (slot_disabled(s, w)) return;
        sfx("click");
        if (!strcmp(w, "throw")) {
            character_unequip(s);
            s->weapon_selected = true;
        } else if (!strcmp(w, "endTurn")) {
            s->can_throw = s->can_shoot = false;
            character_unequip(s);
            s->thrown = s->throw_finished = true;
            s->weapon_selected = true;
        } else if (!strcmp(w, "close")) {
            character_unequip(s);
            W.current->selected = NULL;
        } else {
            character_equip(s, character_first_weapon(s, weapon_from_name(w)));
            s->weapon_selected = true;
        }
    }
}

static void weapons_draw(void) {
    Character *s = sel();
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    bool blue = team_no() == 2;
    const char *title = "choose a weapon";
    if (hover_slot >= 0 && !slot_disabled(s, slots[hover_slot])) title = desc_of(slots[hover_slot], 1);
    ui_text("wp:title", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, CTX_Y + 6, title);
    for (int i = 0; i < 18; i++) {
        float x, y;
        slot_rect(i, &x, &y);
        const char *w = slots[i];
        char key[24];
        snprintf(key, sizeof key, "wb:%d", i);
        MC *b = ui_mc(key, "weapons icon");
        bool dis = slot_disabled(s, w);
        char lab[24];
        if (dis) snprintf(lab, sizeof lab, "disabled");
        else snprintf(lab, sizeof lab, "%s_%s", blue ? "blue" : "red", i == hover_slot ? "over" : "up");
        mc_goto_label(b, lab, false);
        MC *icon = mc_get(b, "icon"), *cnt = mc_get(b, "count"), *inf = mc_get(b, "infinity");
        bool special = !strcmp(w, "throw") || !strcmp(w, "endTurn") || !strcmp(w, "close");
        if (special) {
            mc_set_visible(icon, false);
            mc_set_visible(cnt, false);
            mc_set_visible(inf, false);
        } else {
            WType wt = weapon_from_name(w);
            int n = character_count_weapon(s, wt);
            bool infinite = character_weapon_infinite(s, wt);
            mc_set_visible(icon, true);
            mc_goto_label(icon, w, false);
            if (cnt) {
                char num[8];
                snprintf(num, sizeof num, "%s%d", n >= 10 ? "" : "0", n);
                mc_set_text(cnt, num);
                mc_set_visible(cnt, !infinite && !dis);
            }
            mc_set_visible(inf, infinite && !dis);
        }
        ui_draw(b, x, y);
        // Iconos propios (mismo marco y grises que los de las armas), en la posición del icono
        CX dim = {0.55f, 0.55f, 0.55f, 1, 0, 0, 0, 0};
        if (!strcmp(w, "throw")) ui_image_cx("icon_throw", x + 3, y + 3, dis ? &dim : NULL);
        else if (!strcmp(w, "endTurn")) ui_image("icon_flag", x + 3, y + 3);
        else if (!strcmp(w, "close")) ui_image_cx("icon_close", x + 3, y + 3, dis ? &dim : NULL);
    }
}

// Panel con la descripción completa en la pantalla de arriba mientras se toca un arma
static void weapon_info_top(void) {
    if (mode != M_WEAPONS || hover_slot < 0) return;
    Character *s = sel();
    if (!s || slot_disabled(s, slots[hover_slot])) return;
    const char *d = desc_of(slots[hover_slot], 2);
    int lines = 1;
    for (const char *c = d; *c; c++) lines += *c == '|';
    // Arriba de la pantalla, para no tapar al pirata ni el mensaje de turno
    float w = TOP_W - 40, h = 12 + 22 + 8 + lines * 13 + 8, x = 20, y = 6;
    panel(x, y, w, h);
    ui_text("wi:t", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, y + 10, desc_of(slots[hover_slot], 1));
    ui_text("wi:d", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, y + 12 + 22 + 8, d);
}

// ------------------------------------------------------------ apuntar y lanzar (sin tirachinas)
#define BACK_X (CTX_X + 8)
#define BACK_Y (CTX_Y + CTX_H - 34)

static void aim_update(void) {
    Character *s = sel();
    Weapon *w = s->weapon;
    Solid *o = w ? &w->s : &s->s;
    aim_obj = o;
    if (!aim_on) {
        W.mouse_x = o->clip.x;
        W.mouse_y = o->clip.y;
    }
    bool can_back = !s->weapon_locked;
    if (!aim_on && can_back && (ui_tap(BACK_X, BACK_Y, 56, 26) || (ui_in()->down & BTN_B))) {
        s->weapon_selected = false;
        character_unequip(s);
        return;
    }
    // El gesto empieza en el panel (el minimapa queda para la cámara); luego puede salir de él
    if (ui_touch_began(CTX_X, CTX_Y, CTX_W, CTX_H) && !(can_back && ui_touch_began(BACK_X, BACK_Y, 56, 26)) && o->twangable) {
        aim_on = true;
        aim_sx = ui_tx();
        aim_sy = ui_ty();
        W.twanging = o;
        o->drag_x = o->clip.x;
        o->drag_y = o->clip.y;
        pull_x = pull_y = 0;
    }
    if (!aim_on) return;
    if (ui_touching()) {
        pull_x = (float)(ui_tx() - aim_sx);
        pull_y = (float)(ui_ty() - aim_sy);
        float d = sqrtf(pull_x * pull_x + pull_y * pull_y);
        if (d > PULL_R) { pull_x *= PULL_R / d; pull_y *= PULL_R / d; }
        float k = o->twang_max * 4 / PULL_R;
        W.mouse_x = o->clip.x + pull_x * k;
        W.mouse_y = o->clip.y + pull_y * k;
        return;
    }
    aim_on = false;
    if (sqrtf(pull_x * pull_x + pull_y * pull_y) < 5) {
        // Toque sin estirar: no gasta el turno
        W.twanging = NULL;
        if (o->clip.holder) mc_gclear(o->clip.holder);
        if (o->pin_shown) { clip_destroy(&o->pin); o->pin_shown = false; }
    } else {
        tiles_mouse_up();
    }
    pull_x = pull_y = 0;
}

// Icono del arma (o del pirata) en su marco de la rejilla
static void weapon_slot_icon(Character *s, float x, float y) {
    MC *b = ui_mc("aim:frame", "weapons icon");
    mc_goto_label(b, team_no() == 2 ? "blue_up" : "red_up", false);
    MC *ico = mc_get(b, "icon");
    mc_set_visible(mc_get(b, "count"), false);
    mc_set_visible(mc_get(b, "infinity"), false);
    if (s->weapon) {
        mc_set_visible(ico, true);
        mc_goto_label(ico, weapon_names[s->weapon->type], false);
    } else {
        mc_set_visible(ico, false);
    }
    ui_draw(b, x, y);
    if (!s->weapon) ui_image("icon_throw", x + 3, y + 3);
}

static void aim_draw(void) {
    Character *s = sel();
    Weapon *w = s->weapon;
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    const char *wn = w ? weapon_names[w->type] : "throw";
    weapon_slot_icon(s, CTX_X + 12, CTX_Y + 12);
    // Título con la fuente grande si cabe; si no (p. ej. "throw character"), con la pequeña
    const char *tt = desc_of(wn, 1);
    if (ui_text_width("aim:tw", FONT_PIRATE, tt) <= CTX_W - 62) ui_text("aim:t", FONT_PIRATE, ALIGN_LEFT, CTX_X + 50, CTX_Y + 14, tt);
    else ui_text("aim:ts", FONT_DANGLE, ALIGN_LEFT, CTX_X + 50, CTX_Y + 20, tt);
    char line[48] = "";
    if (w && w->type == W_PIECESOFEIGHT) snprintf(line, sizeof line, "coin %d of 8", w->times_fired + 1);
    ui_text("aim:n", FONT_DANGLE, ALIGN_LEFT, CTX_X + 50, CTX_Y + 40, line);
    // Solo el panel para jalar: la fuerza y la dirección ya se ven arriba
    ui_text("aim:h", FONT_DANGLE, ALIGN_CENTER, CTX_X + CTX_W / 2 + 30, CTX_Y + CTX_H - 30,
            aim_on ? "let go to throw" : "touch here, pull back|and let go to throw");
    if (!s->weapon_locked && !aim_on) button("aim:back", "btn_gray", BACK_X, BACK_Y, 56, 26, "back", FONT_DANGLE);
    if (aim_on) {
        float sx = (float)aim_sx, sy = (float)aim_sy, ex = sx + pull_x, ey = sy + pull_y;
        ui_line(sx, sy, ex, ey, 1, 0xA0F8F8F8);
        ui_rect(floorf(ex) - 2, floorf(ey) - 2, 5, 5, COL_OUTLINE);
        ui_rect(floorf(ex) - 1, floorf(ey) - 1, 3, 3, COL_WHITE);
    }
}

// ------------------------------------------------------------ colocar con cursor (cajas, ancla, gaviota)
// Título centrado arriba y debajo "back" y la acción, del mismo tamaño, centrados como pareja
// en el hueco que queda (o la acción sola en el centro si ya no se puede volver)
#define TG_BTN_W 96
#define TG_BTN_H 30
#define TG_GAP 16
#define TG_BTN_Y floorf(CTX_Y + 18 + (CTX_H - 18 - TG_BTN_H) / 2.0f)
#define TG_BACK_X floorf(BOT_W / 2.0f - TG_BTN_W - TG_GAP / 2.0f)
static bool tg_has_back(Weapon *w) { return !w->fired && !sel()->weapon_locked; }
static float tg_act_x(Weapon *w) { return tg_has_back(w) ? BOT_W / 2.0f + TG_GAP / 2.0f : floorf((BOT_W - TG_BTN_W) / 2.0f); }

static void target_update(Weapon *w) {
    const Input *in = ui_in();
    Character *s = sel();
    if (prev_mode != M_TARGET) {
        cur_x = s->s.clip.x + 64 * (s->team->number == 1 ? 1 : -1);
        cur_y = s->s.clip.y - 40;
    }
    float spd = (in->held & BTN_Y) ? 1 : 4;
    if (in->held & BTN_LEFT) cur_x -= spd;
    if (in->held & BTN_RIGHT) cur_x += spd;
    if (in->held & BTN_UP) cur_y -= spd;
    if (in->held & BTN_DOWN) cur_y += spd;
    cur_x += in->cx * 6;
    cur_y += in->cy * 6;
    float wx, wy;
    if (map_touch(&wx, &wy)) { cur_x = wx; cur_y = wy; }
    if (cur_x < -64) cur_x = -64;
    if (cur_x > (W.lw << 5) + 64) cur_x = (float)((W.lw << 5) + 64);
    if (cur_y < -200) cur_y = -200;
    if (cur_y > W.water_y) cur_y = W.water_y;
    W.mouse_x = cur_x;
    W.mouse_y = cur_y;
    W.mouse_visible = true;
    W.pan_to = NULL;
    tiles_pan_towards(cur_x - VIEW_CX, cur_y - VIEW_CY, 24);
    cur_valid = !(w->type == W_WOODENCRATE || w->type == W_GUNPOWDERBARREL) || box_can_place(cur_x, cur_y);
    float bx = tg_act_x(w), by = TG_BTN_Y;
    bool press = ui_hover(bx, by, TG_BTN_W, TG_BTN_H) || (in->held & BTN_A);
    bool began = ui_touch_began(bx, by, TG_BTN_W, TG_BTN_H) || (in->down & BTN_A);
    if (began && cur_valid) W.mouse_down = true;
    else if (!press) W.mouse_down = false;
    if (tg_has_back(w) && (ui_tap(TG_BACK_X, TG_BTN_Y, TG_BTN_W, TG_BTN_H) || (in->down & BTN_B))) {
        s->weapon_selected = false;
        character_unequip(s);
        W.mouse_visible = false;
    }
}

static void target_draw(Weapon *w) {
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    const char *act = "place";
    if (w->type == W_ANCHOR) act = "drop";
    if (w->type == W_SEAGULL) act = "fly";
    char t[64];
    snprintf(t, sizeof t, "%s", weapon_title(w->type));
    if (w->type == W_WOODENCRATE || w->type == W_GUNPOWDERBARREL) {
        Weapon *b = w;
        while (b->fired && b->next_box) b = b->next_box;
        snprintf(t, sizeof t, "%s - %d left", weapon_title(w->type), b->fired ? 0 : b->create_more + 1);
    }
    ui_text("tg:title", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, CTX_Y + 8, t);
    if (tg_has_back(w)) button("tg:back", "btn_gray", TG_BACK_X, TG_BTN_Y, TG_BTN_W, TG_BTN_H, "back", FONT_DANGLE);
    button("tg:act", cur_valid ? btn_style() : "btn_gray", tg_act_x(w), TG_BTN_Y, TG_BTN_W, TG_BTN_H, act, FONT_DANGLE);
}

// Ayuda del cursor en la pantalla de arriba
static void target_hint_top(void) {
    if (mode != M_TARGET) return;
    const char *h = cur_valid ? "tap the map below or use the d-pad" : "you can't place it there";
    ui_rect(0, TOP_H - 22, TOP_W, 22, 0x90101018);
    ui_text("tg:top", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, TOP_H - 17, h);
}

// ------------------------------------------------------------ acciones (plátano, abanico, gaviota, ola)
#define ACT_Y (CTX_Y + 34)
// Botón de acción (plátano, gaviota, ola): tamaño normal, centrado en el panel; la pista va
// encima y "or press a" debajo, a la misma distancia
// (alto como el botón grande original y ancho justo para la palabra)
#define ACT_BTN_W act_btn_w()
#define ACT_BTN_H 40
#define ACT_BTN_X floorf((BOT_W - ACT_BTN_W) / 2.0f)
#define ACT_BTN_Y floorf(CTX_Y + (CTX_H - ACT_BTN_H) / 2.0f)
static const char *act_label(void) {
    Character *s = sel();
    WType t = s && s->weapon ? s->weapon->type : W_NONE;
    return t == W_BANANA ? "boom" : t == W_SEAGULL ? "poop" : "go";  // sin "!": la fuente pirata no lo tiene
}
static float act_btn_w(void) {
    float w = ceilf(ui_text_width("ac:b:w", FONT_PIRATE, act_label())) + 40;
    w = 2 * ceilf(w / 2);
    return w < 96 ? 96 : w;
}

static void action_update(Weapon *w) {
    const Input *in = ui_in();
    W.mouse_visible = false;
    if (w->type == W_PARACHUTEBOMB) {
        bool l = ui_hover(CTX_X + 6, ACT_Y - 6, 148, 60) || (in->held & BTN_LEFT);
        bool r = ui_hover(CTX_X + 158, ACT_Y - 6, 148, 60) || (in->held & BTN_RIGHT);
        fan_side = l ? -1 : r ? 1 : 0;
        // El original empuja la bomba hacia el lado contrario al ratón: el abanico sopla desde ahí
        if (l) { W.mouse_x = w->s.clip.x + 40; W.mouse_down = true; }
        else if (r) { W.mouse_x = w->s.clip.x - 40; W.mouse_down = true; }
        else W.mouse_down = false;
        return;
    }
    if (w->type == W_TIDALWAVE && !w->fired && !sel()->weapon_locked && (ui_tap(CTX_X + 8, CTX_Y + CTX_H - 34, 56, 26) || (in->down & BTN_B))) {
        sel()->weapon_selected = false;
        character_unequip(sel());
        return;
    }
    bool press = ui_hover(ACT_BTN_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H) || (in->held & BTN_A);
    bool began = ui_touch_began(ACT_BTN_X, ACT_BTN_Y, ACT_BTN_W, ACT_BTN_H) || (in->down & BTN_A);
    if (began) W.mouse_down = true;
    else if (!press) W.mouse_down = false;
}

static void action_draw(Weapon *w) {
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    if (w->type == W_PARACHUTEBOMB) {
        ui_text("ac:t", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, CTX_Y + 10, "hold to blow the parachute");
        bool l = fan_side < 0, r = fan_side > 0;
        CX on = {1, 1, 1, 1, 30, 30, 30, 0};
        const char *big = team_no() == 2 ? "btn_blue_big" : "btn_red_big";
        ui_image_cx(big, CTX_X + 6, ACT_Y + (l ? 1 : 0), l ? &on : NULL);
        ui_image_cx(big, CTX_X + 158, ACT_Y + (r ? 1 : 0), r ? &on : NULL);
        ui_image("arrow_left", CTX_X + 14, ACT_Y + 7 + (l ? 1 : 0));
        ui_image("arrow_right", CTX_X + 284, ACT_Y + 7 + (r ? 1 : 0));
        ui_text("ac:l", FONT_PIRATE, ALIGN_CENTER, CTX_X + 86, ACT_Y + 11 + (l ? 1 : 0), "left");
        ui_text("ac:r", FONT_PIRATE, ALIGN_CENTER, CTX_X + 232, ACT_Y + 11 + (r ? 1 : 0), "right");
        ui_text("ac:k", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, CTX_Y + 86, "or hold left and right on the d-pad");
        return;
    }
    const char *lab = act_label();
    const char *hint = w->type == W_BANANA ? "tap to explode the banana" : w->type == W_SEAGULL ? "tap to poop on the enemy" : "send the tidal wave";
    ui_text("ac:h", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, ACT_BTN_Y - 22, hint);
    {
        // Botón grande estirado a lo ancho (nine-slice) con la palabra centrada
        float bw = ACT_BTN_W, bx = ACT_BTN_X, by = ACT_BTN_Y;
        bool hov = ui_hover(bx, by, bw, ACT_BTN_H);
        char img[48];
        snprintf(img, sizeof img, "ui_%s_big", btn_style());
        CX dim = {0.8f, 0.8f, 0.8f, 1, 0, 0, 0, 0};
        ui_nine(pak_link(mc_pak(), img), 10, bx, by + (hov ? 1 : 0), bw, ACT_BTN_H, hov ? &dim : NULL);
        ui_text("ac:b", FONT_PIRATE, ALIGN_CENTER, BOT_W / 2, by + 7 + (hov ? 1 : 0), lab);  // 22 px de letra centrados en la cara (35 px)
    }
    ui_text("ac:a", FONT_DANGLE, ALIGN_CENTER, BOT_W / 2, ACT_BTN_Y + ACT_BTN_H + 10, "or press a");
    if (w->type == W_TIDALWAVE && !w->fired) button("ac:back", "btn_gray", CTX_X + 8, CTX_Y + CTX_H - 34, 56, 26, "back", FONT_DANGLE);
}

// ------------------------------------------------------------ cañón
#define CN_BTN_Y (CTX_Y + 76)

static void cannon_update(Weapon *w) {
    const Input *in = ui_in();
    Character *s = sel();
    W.mouse_visible = false;
    if (!pad_on) {
        if (ui_tap(CTX_X + 8, CN_BTN_Y, 56, 26)) { cannon_move = true; return; }
        if (ui_tap(CTX_X + 68, CN_BTN_Y, 56, 26)) { cannon_move = false; return; }
        if (in->down & BTN_Y) { cannon_move = !cannon_move; return; }
        if ((ui_tap(CTX_X + CTX_W - 64, CN_BTN_Y, 56, 26) || (in->down & BTN_B)) && !w->fired) {
            s->weapon_selected = false;
            character_unequip(s);
            return;
        }
    }
    // El gesto empieza en el panel, fuera de los botones; el minimapa queda para la cámara
    bool on_buttons = ui_touch_began(CTX_X, CN_BTN_Y - 2, CTX_W, 30);
    if (ui_touch_began(CTX_X, CTX_Y, CTX_W, CTX_H) && !on_buttons) {
        pad_on = true;
        if (cannon_move) {
            pad_wx = w->s.clip.x;
            pad_wy = w->s.clip.y;
            // Como TileSystem.mouseDown: punto de partida del arrastre
            w->s.drag_x = w->s.clip.x;
            w->s.drag_y = w->s.clip.y;
        } else {
            // La clavija está en (pin_x, 0) del cañón girado
            float r = w->s.clip.rotation * 3.14159265f / 180;
            pad_wx = w->s.clip.x + cosf(r) * w->pin_x;
            pad_wy = w->s.clip.y + sinf(r) * w->pin_x;
        }
        aim_sx = ui_tx();
        aim_sy = ui_ty();
        W.mouse_x = pad_wx;
        W.mouse_y = pad_wy;
        W.mouse_down = true;
    }
    if (!pad_on) return;
    if (ui_touching()) {
        float k = cannon_move ? 1.5f : 0.6f;
        W.mouse_x = pad_wx + (ui_tx() - aim_sx) * k;
        W.mouse_y = pad_wy + (ui_ty() - aim_sy) * k;
    } else {
        pad_on = false;
        tiles_mouse_up();
    }
}

static void cannon_draw(Weapon *w) {
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    ui_text("cn:t", FONT_PIRATE, ALIGN_LEFT, CTX_X + 12, CTX_Y + 10, "cannon");
    ui_text("cn:h", FONT_DANGLE, ALIGN_LEFT, CTX_X + 12, CTX_Y + 38,
            cannon_move ? "touch here and drag to move|the cannon inside the circle" : "touch here, pull back far|and let go to fire");
    // Selector de modo: el activo va con el color del equipo
    button("cn:move", cannon_move ? btn_style() : "btn_gray", CTX_X + 8, CN_BTN_Y, 56, 26, "move", FONT_DANGLE);
    button("cn:aim", !cannon_move ? btn_style() : "btn_gray", CTX_X + 68, CN_BTN_Y, 56, 26, "aim", FONT_DANGLE);
    button("cn:back", "btn_gray", CTX_X + CTX_W - 64, CN_BTN_Y, 56, 26, "back", FONT_DANGLE);
    if (pad_on) {
        float ex = (float)ui_tx(), ey = (float)ui_ty();
        ui_line((float)aim_sx, (float)aim_sy, ex, ey, 1, 0xA0F8F8F8);
        ui_rect(floorf(ex) - 2, floorf(ey) - 2, 5, 5, COL_OUTLINE);
        ui_rect(floorf(ex) - 1, floorf(ey) - 1, 3, 3, (!cannon_move && w->pin_x < -30) ? COL_RED : COL_WHITE);
    }
}

// ------------------------------------------------------------ muñeco vudú: elegir víctima
static void voodoo_update(Weapon *w) {
    Team *en = &W.teams[2 - W.current->number];
    Character *s = sel();
    if (prev_mode != M_VOODOO) choose_idx = 0;
    int r = cards_pick(en, &choose_idx);
    Character *h = en->chars[choose_idx];
    W.mouse_x = h->s.clip.x;
    W.mouse_y = h->s.clip.y;
    W.mouse_visible = true;
    tiles_pan_towards(h->s.clip.x - VIEW_CX, h->s.clip.y - 30 - VIEW_CY, 30);
    if (r == 2) {
        voodoo_set_target(w, h);
        sfx("voodoo");
    }
    if ((ui_in()->down & BTN_B) || ui_tap(CTX_X + 6, CTX_Y + CTX_H - 26, 56, 22)) {
        s->weapon_selected = false;
        character_unequip(s);
        W.mouse_visible = false;
    }
}

// ------------------------------------------------------------ espera / bocadillo
static void wait_draw(bool speech) {
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    const char *t = "";
    if (speech) t = "tap or press a to continue";
    else if (W.current && W.current->ai) t = W.current->ai_finished ? "computer's turn" : "computer is thinking...";
    else if (sel() && sel()->thrown) t = "whoosh!";
    else if (W.current) t = W.current->number == 1 ? "player 1's turn" : "player 2's turn";
    Character *who = speech && W.speech ? W.speech->target : (W.current ? (W.current->selected ? W.current->selected : NULL) : NULL);
    float tx = BOT_W / 2;
    if (who) {
        MC *m = ui_mc(who->s.clip.link, who->s.clip.link);
        mc_goto(m, 1, false);
        Mat mm = {2, 0, 0, 2, CTX_X + 40, floorf(CTX_Y + CTX_H / 2.0f) + 2};
        ui_draw_m(m, &mm, NULL);
        tx = CTX_X + 40 + (CTX_W - 40) / 2;
    }
    ui_text("wt:t", FONT_DANGLE, ALIGN_CENTER, tx, floorf(CTX_Y + CTX_H / 2.0f) - 6, t);
}

// ------------------------------------------------------------ ventana de fin de nivel / pausa
static const char *popup_label(void) {
    if (paused) return "quit_prompt";
    switch (W.result) {
    case 1: return "vs_1p_wins";
    case 2: return "vs_2p_wins";
    case 3: return "vs_draw";
    case 4: return "level_complete_1p";
    case 5: return "level_failed_1p";
    case 6: return "game_complete_1p";
    }
    return "quit_prompt";
}

typedef struct { const char *link; int action; } PopBtn;

static int popup_buttons(PopBtn *b) {
    int n = 0;
    if (paused) {
        b[n++] = (PopBtn){"continue_game", POP_CONTINUE};
        b[n++] = (PopBtn){"restart_level", POP_RESTART};
        b[n++] = (PopBtn){"back to title", POP_QUIT};
        return n;
    }
    switch (W.result) {
    case 4: b[n++] = (PopBtn){"next_level", POP_NEXT}; break;
    case 6: b[n++] = (PopBtn){"congrats_button", POP_CONGRATS}; break;
    default: b[n++] = (PopBtn){"restart_level", POP_RESTART}; break;
    }
    b[n++] = (PopBtn){"back to title", POP_QUIT};
    return n;
}

// Botones originales (barra de 280x24 con el origen arriba), centrados en el panel de contexto
#define POP_BAR_H 24
#define POP_GAP 15
static float popup_y(int i, int n) {
    float total = n * POP_BAR_H + (n - 1) * POP_GAP;
    return floorf(CTX_Y + (CTX_H - total) / 2) + i * (POP_BAR_H + POP_GAP);
}

static void popup_update(void) {
    PopBtn b[4];
    int n = popup_buttons(b);
    const Input *in = ui_in();
    if (popup_fresh) { popup_fresh = false; return; }
    if (popup_guard > 0) { popup_guard--; return; }
    if (!popup_armed) {
        if (!ui_touching() && !(in->held & BTN_A)) popup_armed = true;
        return;
    }
    for (int i = 0; i < n; i++)
        if (ui_tap(BOT_W / 2 - 140, popup_y(i, n) - 3, 280, POP_BAR_H + 6)) popup_action = b[i].action;
    if (in->down & BTN_A) popup_action = b[0].action;
    if (paused && (in->down & (BTN_B | BTN_START))) popup_action = POP_CONTINUE;
    if (popup_action == POP_CONTINUE) {
        paused = false;
        W.popup = false;
    }
    if (popup_action) sfx("click");
}

static void popup_draw_bottom(void) {
    panel(CTX_X, CTX_Y, CTX_W, CTX_H);
    PopBtn b[4];
    int n = popup_buttons(b);
    for (int i = 0; i < n; i++) {
        float y = popup_y(i, n);
        bool hov = ui_hover(BOT_W / 2 - 140, y - 3, 280, POP_BAR_H + 6);
        MC *m = ui_mc(b[i].link, b[i].link);
        mc_goto_label(m, hov ? "over" : "up", false);
        ui_draw(m, BOT_W / 2, y);
    }
}

// Fila de un equipo: nombre, piratas en pie y vida total
static void team_row(int t, float x, float y, float w) {
    Team *tm = &W.teams[t];
    float h = 0, m = 0;
    for (int i = 0; i < tm->n; i++) { h += tm->chars[i]->health; m += tm->chars[i]->max_health; }
    char k[24], txt[48];
    ui_rect(x, y + 1, 9, 9, COL_OUTLINE);
    ui_rect(x + 1, y + 2, 7, 7, team_col(t + 1));
    snprintf(k, sizeof k, "pp:n%d", t);
    ui_text(k, FONT_DANGLE, ALIGN_LEFT, x + 14, y, tm->ai ? "computer" : (t == 0 ? "player 1" : "player 2"));
    snprintf(k, sizeof k, "pp:a%d", t);
    snprintf(txt, sizeof txt, "%d of %d pirates", team_count_alive(tm), tm->n);
    ui_text(k, FONT_DANGLE, ALIGN_CENTER, x + w * 0.53f, y, txt);
    snprintf(k, sizeof k, "pp:h%d", t);
    snprintf(txt, sizeof txt, "health %d", m > 0 ? (int)roundf(100 * h / m) : 0);  // la fuente no tiene '%'
    ui_text(k, FONT_DANGLE, ALIGN_RIGHT, x + w, y, txt);
}

static void popup_draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, 0x8C101018);
    bool scores = !paused && (W.result == 4 || W.result == 5 || W.result == 6);
    float pw = 360, ph = scores ? 184 : 150, px = (TOP_W - pw) / 2, py = floorf((TOP_H - ph) / 2);
    panel(px, py, pw, ph);
    MC *p = ui_mc("popup", "popup");
    mc_goto_label(p, popup_label(), false);
    // Título del popup original; los botones van a la pantalla táctil
    const char *title = "";
    for (MC *c = p->child; c; c = c->next) {
        if (c->is_text && c->font == FONT_PIRATE) title = mc_text(c);
        mc_set_visible(c, c->is_text && c->font == FONT_DANGLE);
    }
    ui_text("pp:title", FONT_PIRATE, ALIGN_CENTER, TOP_W / 2, py + 14, paused ? "game paused" : title);
    char line[64];
    int turns = W.teams[0].turns > W.teams[1].turns ? W.teams[0].turns : W.teams[1].turns;
    if (W.game_type == 2) snprintf(line, sizeof line, "battle map %d of 18   -   turn %d", W.level - 15, turns);
    else snprintf(line, sizeof line, "level %d of 15   -   turn %d", W.level, turns);
    ui_text("pp:info", FONT_DANGLE, ALIGN_CENTER, TOP_W / 2, py + 46, line);
    if (scores) {
        // Puntuaciones del popup original (levelScore/totalScore o finalScore)
        MC *ls = mc_get(p, "levelScore"), *ts = mc_get(p, "totalScore"), *fs = mc_get(p, "finalScore");
        extern int app_total_score(void);
        char num[16];
        if (ls) { snprintf(num, sizeof num, "%d", (int)W.score_gain); mc_set_text(ls, num); }
        if (ts) { snprintf(num, sizeof num, "%d", app_total_score()); mc_set_text(ts, num); }
        if (fs) { snprintf(num, sizeof num, "%d", app_total_score()); mc_set_text(fs, num); }
        ui_draw(p, TOP_W / 2, py + 120);
    }
    ui_rect(px + 16, py + ph - 62, pw - 32, 1, 0xFF4A4448);
    team_row(0, px + 24, py + ph - 52, pw - 48);
    team_row(1, px + 24, py + ph - 30, pw - 48);
}

// ------------------------------------------------------------ bucle
static void set_layout(void) {
    bool big = mode == M_WAIT || mode == M_SPEECH || mode == M_TARGET;
    // En las ventanas de pausa/fin el mapa encoge un poco para que los botones respiren
    map_h = big ? 130 : mode == M_POPUP ? 62 : 78;
    ctx_y = MAP_Y + map_h + 3;
    ctx_h = BOT_H - 3 - ctx_y;
}

void hud_update(const Input *in) {
    ui_input(in);
    team_bars_tick();
    prev_mode = mode;
    mode = compute_mode();
    set_layout();
    if (mode == M_POPUP && prev_mode != M_POPUP) {
        popup_guard = W.result ? 45 : 0;
        popup_armed = !W.result;
    }
    if (mode != M_AIM) aim_on = false;
    if (mode != M_CANNON) pad_on = false;
    if (mode != M_TARGET && mode != M_VOODOO) W.mouse_visible = false;
    bool menu_ok = mode != M_POPUP && !W.result && !aim_on && !pad_on;
    if (menu_ok && ((in->down & BTN_START) || header_menu_tap())) {
        hud_pause();
        mode = M_POPUP;
        return;
    }
    Character *s = sel();
    // Tocar y arrastrar en el minimapa mueve la cámara (salvo al colocar, donde mueve el cursor)
    if (mode != M_TARGET && mode != M_POPUP && mode != M_SPEECH && !aim_on && !pad_on) {
        float wx, wy;
        if (map_touch(&wx, &wy)) {
            W.pan_to = NULL;
            tiles_pan(wx - VIEW_CX, wy - VIEW_CY, false);
        }
    }
    switch (mode) {
    case M_POPUP: popup_update(); break;
    case M_SPEECH:
        if (ui_touch_began(0, HDR_H, BOT_W, BOT_H - HDR_H) || (in->down & BTN_A)) tiles_mouse_down();
        break;
    case M_CHOOSE: {
        int r = cards_pick(W.current, &choose_idx);
        if (r) W.pan_to = W.current->chars[choose_idx];
        if (r == 2) team_select(W.current, W.current->chars[choose_idx], false);
        break;
    }
    case M_WEAPONS: weapons_update(); break;
    case M_AIM: aim_update(); break;
    case M_TARGET: target_update(s->weapon); break;
    case M_ACTION: action_update(s->weapon); break;
    case M_CANNON: cannon_update(s->weapon); break;
    case M_VOODOO: voodoo_update(s->weapon); break;
    default: W.mouse_down = false; break;
    }
    if (mode != M_ACTION && mode != M_TARGET && mode != M_CANNON && !aim_on) W.mouse_down = false;
    fan_on = fan_on && mode == M_ACTION;
}

void hud_render_bottom(void) {
    // El turno puede haber cambiado en la lógica después de hud_update: no dibujar con un
    // pirata que ya no está seleccionado (causaba los cuelgues al elegir pirata)
    int m = compute_mode();
    if (m != mode) { prev_mode = mode; mode = m; set_layout(); }
    Character *s = sel();
    ui_image("bg_deck", 0, 0);
    header_draw(mode != M_POPUP);
    map_draw(mode == M_TARGET);
    switch (mode) {
    case M_POPUP: popup_draw_bottom(); break;
    case M_CHOOSE: cards_draw(W.current, choose_idx, "tap a pirate, or use l r and a"); break;
    case M_WEAPONS: weapons_draw(); break;
    case M_AIM: aim_draw(); break;
    case M_TARGET: target_draw(s->weapon); break;
    case M_ACTION: action_draw(s->weapon); break;
    case M_CANNON: cannon_draw(s->weapon); break;
    case M_VOODOO:
        cards_draw(&W.teams[2 - W.current->number], choose_idx, "tap an enemy");
        button("vd:back", "btn_gray", CTX_X + 6, CTX_Y + CTX_H - 28, 56, 26, "back", FONT_DANGLE);
        break;
    default: wait_draw(mode == M_SPEECH); break;
    }
}

void hud_render_top(void) {
    Mat cam = mat_translate(mc_x(W.content), mc_y(W.content));
    Character *s = sel();
    Weapon *w = s ? s->weapon : NULL;
    if (mode == M_TARGET && w) {
        float x = cur_x + cam.tx, y = cur_y + cam.ty;
        MC *c = ui_mc("cursor", "cursor");
        const char *lab = weapon_names[w->type];
        if ((w->type == W_WOODENCRATE || w->type == W_GUNPOWDERBARREL) && !cur_valid) lab = "cross";
        mc_goto_label(c, lab, false);
        ui_draw(c, floorf(x), floorf(y));
    } else if (mode == M_ACTION && w && w->type == W_PARACHUTEBOMB && fan_side) {
        MC *c = ui_mc("cursorfan", "cursor");
        mc_goto_label(c, "fan", false);
        MC *f = mc_get(c, "fan");
        if (f) { if (fan_on) mc_play(f); else mc_stop(f); }
        float x = w->s.clip.x + cam.tx + (fan_side < 0 ? 40 : -40), y = w->s.clip.y + cam.ty;
        mc_set_rotation(c, fan_side < 0 ? -90 : 90);
        ui_draw(c, floorf(x), floorf(y));
    } else if (mode == M_VOODOO && w) {
        MC *c = ui_mc("cursorvd", "cursor");
        mc_goto_label(c, "voodooDoll", false);
        ui_draw(c, floorf(W.mouse_x + cam.tx), floorf(W.mouse_y + cam.ty - 30));
    }
    weapon_info_top();
    target_hint_top();
    if (mode == M_POPUP) popup_draw_top();
}
