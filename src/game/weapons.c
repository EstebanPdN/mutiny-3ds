// Weapon y sus subclases (CherryBomb, Dynamite, Boulder, PiecesOfEight, RumBottle, Banana,
// ParachuteBomb, WoodenCrate/GunpowderBarrel, Seagull, Mine, Cannon, Cannonball, Anchor,
// VoodooDoll, TidalWave).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define SH5(v) ((int)(v) >> 5)

float trig_sin(int d);
float trig_cos(int d);

static const char *link_of(WType t) {
    switch (t) {
    case W_CANNONBALL: return "cannonball";
    case W_SEAGULLFIRE: return "seagullFire";
    case W_SIMSHOT: return "";
    default: return weapon_names[t];
    }
}

static void whiteout(MC *mc, float v) {
    CX c;
    if (v > 0.5f) {
        float k = (v - 0.5f) * 2, add = 255 - k * 255;
        CX t = {k, k, k, 1, add, add, add, 0};
        c = t;
    } else {
        CX t = {0, 0, 0, v * 2, 255, 255, 255, 0};
        c = t;
    }
    mc_set_cx(mc, &c);
}

Weapon *weapon_new(WType t) {
    Weapon *w = world_alloc(sizeof *w);
    solid_init(&w->s, W.characters, link_of(t), SO_WEAPON);
    w->type = t;
    w->limited_to_turn = true;
    w->drag_range = 130;
    w->drag_offset = -100;
    w->show_circle = true;
    w->last_sq = INFINITY;
    Solid *s = &w->s;
    switch (t) {
    case W_CHERRYBOMB:
        clip_show(&s->clip);
        s->left = s->right = s->top = s->bottom = 9;
        s->hits_boxes = true; s->draggable = false; s->twangable = true;
        break;
    case W_DYNAMITE:
        s->left = s->right = s->top = s->bottom = 11;
        clip_show(&s->clip);
        s->friction = 1.7f;
        s->hits_boxes = true;
        mc_goto_label(s->clip.mc, "lit", true);
        s->draggable = false; s->twangable = true;
        break;
    case W_RUMBOTTLE:
        clip_show(&s->clip);
        s->left = s->right = s->top = s->bottom = 14;
        s->hits_boxes = true; s->draggable = false; s->twangable = true;
        s->twang_max = 30;
        break;
    case W_BANANA:
        s->left = s->right = s->top = s->bottom = 7;
        clip_show(&s->clip);
        s->bounce = 0.8f; s->friction = 0.5f;
        s->hits_boxes = true; s->draggable = false; s->twangable = true;
        s->twang_max = 30;
        break;
    case W_BOULDER:
        s->left = s->right = s->top = s->bottom = 31;
        clip_show(&s->clip);
        s->friction = 0.25f; s->weight = 1.5f;
        s->hits_boxes = true; s->draggable = false; s->twangable = true;
        w->visibility = 2;
        break;
    case W_ANCHOR:
        s->left = s->right = 48;
        s->top = 96;
        s->bottom = 0;
        w->placeable = true;
        s->hits_boxes = true;
        w->show_circle = false;
        s->draggable = false;
        w->hold_time = 30;
        w->fade_time = 10;
        break;
    case W_WOODENCRATE:
    case W_GUNPOWDERBARREL:
        s->left = s->top = 16;
        s->right = s->bottom = 15;
        w->create_more = t == W_GUNPOWDERBARREL ? 1 : 2;
        w->limited_to_turn = false;
        s->draggable = false;
        s->hits_boxes = true;
        w->show_circle = false;
        break;
    case W_CANNON:
        clip_show(&s->clip);
        w->placeable = true;
        s->weight = 0;
        s->hits_boxes = true;
        w->visibility = 2;
        w->pin_x = -21;
        break;
    case W_CANNONBALL:
        clip_show(&s->clip);
        s->weight = 0;
        s->hits_boxes = true;
        break;
    case W_MINE:
        s->left = s->right = s->top = s->bottom = 14;
        s->friction = 1.5f;
        clip_show(&s->clip);
        w->limited_to_turn = false;
        s->hits_boxes = true;
        w->drag_range = 180;
        {
            static const int bt[10] = {0, 15, 30, 38, 45, 49, 53, 55, 57, 59};
            memcpy(w->beeps, bt, sizeof bt);
            w->nbeeps = 10;
        }
        mc_goto_label(s->clip.mc, "in_throw", false);
        s->draggable = false; s->twangable = true;
        w->ignore_time = 10;
        w->countdown = 60;
        break;
    case W_PIECESOFEIGHT:
        s->left = s->right = s->top = s->bottom = 7;
        clip_show(&s->clip);
        s->hits_boxes = true; s->draggable = false; s->twangable = true;
        break;
    case W_PARACHUTEBOMB:
        s->left = s->right = s->top = s->bottom = 11;
        clip_show(&s->clip);
        s->hits_boxes = true; s->draggable = false; s->twangable = true;
        s->twang_max = 30;
        break;
    case W_SEAGULL:
        w->placeable = true;
        s->hits_tiles = false;
        s->weight = 0;
        w->show_circle = false;
        s->draggable = false;
        clip_init(&w->dotted, W.characters, "dottedLine", CK_PLAIN, NULL);
        w->dotted.x = 0;
        w->dotted.y = W.mouse_y;
        clip_show(&w->dotted);
        break;
    case W_TIDALWAVE:
        w->placeable = true;
        s->hits_tiles = false;
        s->weight = 0;
        w->show_circle = false;
        s->draggable = false;
        break;
    case W_VOODOODOLL:
        clip_show(&s->clip);
        s->draggable = false;
        s->hits_boxes = true;
        w->frames_on_this = w->frames_on_target = 10;
        break;
    default:
        break;
    }
    return w;
}

void weapon_update(Weapon *w) {
    clip_update(&w->s.clip);
    w->track_x = w->s.clip.x;
    w->track_y = w->s.clip.y;
    if (w->type == W_CANNON && w->cannonball) {
        w->track_x = w->cannonball->s.clip.x;
        w->track_y = w->cannonball->s.clip.y;
        w->track = true;
    }
}

void weapon_destroy(Weapon *w) {
    if (w->type == W_SEAGULL) clip_destroy(&w->dotted);
    if (w->s.pin_shown) { clip_destroy(&w->s.pin); w->s.pin_shown = false; }
    clip_destroy(&w->s.clip);
    if (W.twanging == &w->s) W.twanging = NULL;
    if (W.dragging == &w->s) W.dragging = NULL;
}

static void smoke(Weapon *w) {
    if (!w->simulation && !w->finished) debris_new(W.effects, "cannonSmokeTrail", w->s.clip.x, w->s.clip.y);
}

static void explode_here(Weapon *w, float size, float dmg) {
    w->finished = true;
    clip_hide(&w->s.clip);
    explosion_new(w->s.clip.x, w->s.clip.y, size, dmg, w->owner);
    sfx("pop");
}

// ------------------------------------------------------------ advanceMotion (con sobrecargas)
static bool stopped(Weapon *w, float lim) { return w->s.vx == 0 && fabsf(w->s.vy) < lim; }

static void mine_explode(Weapon *w);

static void mine_proximity(Weapon *w) {
    if (!w->fired || w->active) return;
    if (--w->ignore_time > 0) return;
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < W.teams[t].n; i++) {
            Character *c = W.teams[t].chars[i];
            if (!c->alive) continue;
            if (c->s.vx == 0 && fabsf(c->s.vy) <= 0.2f && W.twanging != &c->s) continue;
            float dx = c->s.clip.x - w->s.clip.x, dy = c->s.clip.y + c->s.bottom - w->s.clip.y;
            if (dx * dx + dy * dy < 3600) {
                w->active = true;
                mc_goto_label(w->s.clip.mc, "warn", true);
                return;
            }
        }
}

void weapon_motion(Weapon *w) {
    Solid *s = &w->s;
    switch (w->type) {
    case W_DYNAMITE:
        if (w->fired) s->clip.rotation += s->vx * 2;
        solid_base_motion(s);
        if (stopped(w, 0.2f)) {
            if (w->simulation) w->sim_finished = true;
            else if (w->fired && !w->finished) explode_here(w, 250, 70);
        }
        if (!w->simulation && s->clip.y > W.water.y) mc_goto_label(s->clip.mc, "unlit", false);
        return;
    case W_BANANA: {
        if (w->fired) s->clip.rotation += s->vx * 2;
        solid_base_motion(s);
        bool boom = stopped(w, 0.5f);
        if (!boom) {
            if (w->owner && w->owner->team->ai) {
                float best = INFINITY;
                for (int t = 0; t < 2; t++)
                    for (int i = 0; i < W.teams[t].n; i++) {
                        Character *c = W.teams[t].chars[i];
                        float dx = c->s.clip.x - s->clip.x, dy = c->s.clip.y - s->clip.y, d = dx * dx + dy * dy;
                        if (d < best) best = d;
                    }
                if (best > w->last_sq && best < 2500) boom = true;
                if (best < 400) boom = true;
                // En el original lastSqDistance nunca se actualiza (sigue en Infinity)
            } else if (W.mouse_down) {
                boom = true;
            }
        }
        if (boom) {
            if (w->simulation) w->sim_finished = true;
            else if (w->fired && !w->finished) explode_here(w, 160, 80);
        }
        return;
    }
    case W_BOULDER:
        if (w->fired && s->clip.mc) {
            MC *r = mc_get(s->clip.mc, "rotating");
            if (r) mc_set_rotation(r, r->rot + s->vx * 2.5f);
        }
        solid_base_motion(s);
        if (w->fired) {
            if (stopped(w, 0.5f)) {
                if (w->simulation) w->sim_finished = true;
                else {
                    w->visibility -= 0.1f;
                    if (w->visibility < 1 && s->clip.mc) whiteout(s->clip.mc, w->visibility);
                    if (w->visibility < 0) w->finished = true;
                }
            }
            for (int t = 0; t < 2; t++)
                for (int i = 0; i < W.teams[t].n; i++) {
                    Character *c = W.teams[t].chars[i];
                    if (!c->alive || c == w->owner) continue;
                    if (c->s.clip.y - c->s.top <= s->clip.y + 32 && c->s.clip.y + c->s.bottom >= s->clip.y - 32 &&
                        fabsf(c->s.clip.x - s->clip.x) <= 32) {
                        if (c->s.clip.x > s->clip.x) {
                            c->s.clip.x = s->clip.x + 32;
                            if (s->vx > 0) c->s.vx += s->vx;
                        } else {
                            c->s.clip.x = s->clip.x - 32;
                            if (s->vx < 0) c->s.vx += s->vx;
                        }
                        character_subtract_health(c, fabsf(s->vx) * 1.5f);
                    }
                }
        }
        return;
    case W_WOODENCRATE:
    case W_GUNPOWDERBARREL:
        if (w->fired) {
            solid_base_motion(s);
            weapon_update(w);
        }
        return;
    case W_CANNONBALL: {
        solid_base_motion(s);
        float lw = (float)(W.lw << 5);
        if (s->clip.x < -300 || s->clip.x > lw + 300 || s->clip.y < -300 || s->clip.y > W.water.y) {
            if (w->simulation) w->sim_finished = true;
            else { w->finished = true; clip_hide(&s->clip); }
        }
        return;
    }
    case W_MINE:
        solid_base_motion(s);
        if (w->fired) mine_proximity(w);
        if (stopped(w, 0.2f) || (s->vx == 0 && fabsf(s->vy) < 0.2f)) {
            if (w->simulation) w->sim_finished = true;
            else if (w->fired && !w->finished) {
                w->finished = true;
                if (W.nmines < 32) W.mines[W.nmines++] = w;
                if (!w->active) mc_goto_label(s->clip.mc, "arm", true);
            }
        }
        if (w->active && !w->exploded) {
            if (--w->countdown <= 0) mine_explode(w);
            W.inactivity = 0;
        }
        return;
    case W_PARACHUTEBOMB:
        if (w->fired || w->simulation) {
            if (s->vy > 1) {
                s->vy -= 2;
                if (s->vy < 1) s->vy = 1;
            }
            s->vx *= 0.95f;
            if (!w->chute_open && s->vy > -10 && !w->simulation) {
                w->chute_open = true;
                mc_goto_label(s->clip.mc, "opening", true);
            }
            if (w->fired) w->frames_from_fire++;
        }
        if (w->fired && !w->simulation && w->owner && !w->owner->team->ai) {
            extern void hud_fan(bool on);
            if (W.mouse_down) {
                if (W.mouse_x - s->clip.x < 0) s->vx += 0.2f;
                else s->vx -= 0.2f;
                hud_fan(true);
                if (w->frames_from_fire % 12 == 0) sfx("fan");
            } else {
                hud_fan(false);
            }
        }
        solid_base_motion(s);
        if (s->clip.y < -300) {
            s->clip.y = -300;
            if (s->vy < 0) s->vy = 0;
        }
        return;
    default:
        solid_base_motion(s);
        return;
    }
}

// ------------------------------------------------------------ contact
void weapon_contact(Weapon *w, int side) {
    switch (w->type) {
    case W_CHERRYBOMB:
        if (w->simulation) { w->sim_finished = true; return; }
        if (w->fired && !w->finished) explode_here(w, 80, 40);
        return;
    case W_RUMBOTTLE:
        if (w->simulation) { w->sim_finished = true; return; }
        if (w->fired && !w->finished) {
            explode_here(w, 80, 25);
            if (side == FLOOR) {
                int gx = SH5(w->s.clip.x), gy = SH5(w->s.clip.y) + 1;
                while (tile_at(gx, gy - 1)) gy--;
                flame_new((float)(gx << 5), (float)(gy << 5), true);
                flame_new((float)(gx << 5), (float)(gy << 5), false);
            }
        }
        return;
    case W_BANANA:
        if (w->fired && !w->finished) sfx("banana_bounce");
        return;
    case W_ANCHOR:
        if (w->simulation && side == FLOOR) { w->sim_finished = true; return; }
        if (side == FLOOR) {
            if (!w->hit_bottom) {
                w->hit_bottom = true;
                for (int t = 0; t < 2; t++)
                    for (int i = 0; i < W.teams[t].n; i++) {
                        Character *c = W.teams[t].chars[i];
                        if (fabsf(c->s.clip.x - w->s.clip.x) < 48 && c->s.clip.y < w->s.clip.y && c->s.clip.y > w->s.clip.y - 64)
                            character_subtract_health(c, 60);
                    }
                mc_play(w->s.clip.mc);
            }
            w->s.vy = 0;
            sfx("anchor");
        }
        return;
    case W_CANNONBALL:
        if (w->simulation) { w->sim_finished = true; return; }
        if (w->fired && !w->finished) {
            explode_here(w, 100, 50);
            w->s.vx = w->s.vy = 0;
        }
        return;
    case W_PIECESOFEIGHT:
        if (w->simulation) { w->sim_finished = true; return; }
        if (w->fired && !w->finished) {
            extern void pieces_next(Weapon *w, bool explode);
            pieces_next(w, true);
        }
        return;
    case W_PARACHUTEBOMB:
        if (w->simulation) { w->sim_finished = true; return; }
        if (w->fired && !w->finished) explode_here(w, 160, 50);
        return;
    case W_SEAGULLFIRE:
        if (w->gull) seagull_end_shot(w->gull, w);
        explosion_new(w->s.clip.x, w->s.clip.y, 50, 50, w->owner);
        weapon_destroy(w);
        w->finished = true;
        return;
    case W_SIMSHOT:
        w->sim_finished = true;
        return;
    default:
        return;
    }
}

// ------------------------------------------------------------ release / twang / fire / place
void weapon_fire(Weapon *w, float vx, float vy) {
    if (w->type == W_CANNON) {
        w->fired = true;
        w->cannonball = weapon_new(W_CANNONBALL);
        w->cannonball->s.clip.x = w->s.clip.x;
        w->cannonball->s.clip.y = w->s.clip.y;
        clip_show(&w->cannonball->s.clip);
        weapon_update(w->cannonball);
        w->cannonball->owner = w->owner;
        weapon_fire(w->cannonball, vx, vy);
        sfx("cannon explosion");
        return;
    }
    if (!w->fired) {
        w->s.vx = vx;
        w->s.vy = vy;
        w->fired = true;
        w->track = true;
    }
    if (w->type == W_BANANA) W.mouse_down = false;
}

void weapon_release(Weapon *w) {
    if (w->type == W_CANNON) {
        w->s.vx = w->s.vy = 0;
        return;
    }
    float v = sqrtf(w->s.vx * w->s.vx + w->s.vy * w->s.vy);
    if (v > 20) { w->s.vx = 20 * w->s.vx / v; w->s.vy = 20 * w->s.vy / v; }
    weapon_fire(w, w->s.vx, w->s.vy);
    w->finished = false;
    w->owner->can_shoot = w->owner->can_throw = false;
    if (w->type == W_BOULDER) { w->s.vx *= 0.5f; w->s.vy *= 0.5f; }
}

void weapon_twang(Weapon *w) {
    solid_twang(&w->s);
    w->fired = true;
    w->track = true;
    w->owner->can_shoot = w->owner->can_throw = false;
    if (w->type == W_VOODOODOLL) {
        w->throw_vx = w->s.vx;
        w->throw_vy = w->s.vy;
    }
}

void weapon_place(Weapon *w, float x, float y) {
    switch (w->type) {
    case W_SEAGULL:
        if (!w->fired) {
            w->fired = true;
            w->track = true;
            w->owner->can_shoot = w->owner->can_throw = false;
            w->s.clip.x = x;
            w->s.clip.y = y;
            W.mouse_down = false;
            w->s.vx = 10;
            clip_show(&w->s.clip);
            clip_hide(&w->dotted);
        }
        return;
    case W_WOODENCRATE:
    case W_GUNPOWDERBARREL:
        if (!w->fired) {
            w->s.clip.x = x;
            w->s.clip.y = y;
            w->fired = true;
            w->track = true;
        }
        if (W.nboxes < 64 && !w->in_boxes) { W.boxes[W.nboxes++] = w; w->in_boxes = true; }
        w->fired = true;
        w->track = false;
        w->owner->can_shoot = w->owner->can_throw = false;
        if (w->create_more > 0) {
            w->next_box = weapon_new(w->type);
            w->next_box->owner = w->owner;
            w->next_box->create_more = w->create_more - 1;
        } else {
            w->finished = true;
        }
        return;
    default:
        if (!w->fired) {
            w->s.clip.x = x;
            w->s.clip.y = y;
            w->fired = true;
            w->track = true;
        }
        if (w->type == W_ANCHOR) w->s.clip.y = -200;
        return;
    }
}

// ------------------------------------------------------------ cajas
bool box_can_place(float px, float py) {
    int x0 = SH5(px - 16), x1 = SH5(px + 16), y0 = SH5(py - 16), y1 = SH5(py + 16);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            if (tile_at(x, y)) return false;
    float floor_y = (float)((y1 + 1) << 5);
    bool found = false;
    for (int y = y1; y < W.lh; y++) {
        floor_y = (float)((y + 1) << 5);
        for (int x = x0; x <= x1; x++)
            if (tile_at(x, y)) { found = true; break; }
        if (found) break;
    }
    for (int i = 0; i < W.nboxes; i++) {
        Solid *b = &W.boxes[i]->s;
        if (b->clip.x - b->left <= px + 16 && b->clip.x + b->right >= px - 15 && b->clip.y + b->bottom >= py - 15) {
            if (b->clip.y - b->top < floor_y) floor_y = b->clip.y - b->top;
            if (b->clip.y - b->top <= py + 16) return false;
        }
    }
    for (int i = 0; i < W.nchests; i++) {
        Chest *c = W.chests[i];
        if (c->clip.x - 16 <= px + 16 && c->clip.x + 16 >= px - 15 && c->clip.y + 16 >= py - 16) return false;
    }
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < W.teams[t].n; i++) {
            Character *c = W.teams[t].chars[i];
            if (!c->alive) continue;
            if (c->s.clip.x - c->s.left <= px + 16 && c->s.clip.x + c->s.right >= px - 15 &&
                c->s.clip.y + c->s.bottom >= py - 15 && c->s.clip.y - c->s.top <= floor_y)
                return false;
        }
    return true;
}

void box_explode(Weapon *w) {
    for (int i = W.nboxes - 1; i >= 0; i--)
        if (W.boxes[i] == w) {
            memmove(&W.boxes[i], &W.boxes[i + 1], sizeof W.boxes[0] * (size_t)(W.nboxes - i - 1));
            W.nboxes--;
        }
    w->in_boxes = false;
    mc_goto_label(w->s.clip.mc, "explode", true);
    if (w->type == W_GUNPOWDERBARREL) explosion_new(w->s.clip.x, w->s.clip.y, 150, 30, NULL);
}

int box_ai_simulation(Weapon *w, float out[][2], int max) {
    float avg = 0;
    int cnt = 0;
    for (int i = 0; i < w->owner->team->n; i++)
        if (w->owner->team->chars[i]->alive) { avg += w->owner->team->chars[i]->s.clip.x; cnt++; }
    if (cnt) avg /= cnt;
    Team *en = &W.teams[2 - w->owner->team->number];
    int nalive = 0;
    for (int i = 0; i < en->n; i++) nalive += en->chars[i]->alive;
    int n = 0;
    for (int i = 0; i < 10; i++) {
        int k = (int)floorf(frand() * nalive);
        Character *c = en->chars[k < en->n ? k : 0];  // el original indexa la lista completa
        float sg = avg - c->s.clip.x > 0 ? 1.0f : avg - c->s.clip.x < 0 ? -1.0f : 0.0f;
        float ox = sg * floorf(16 + frand() * 48);
        float oy = floorf(frand() * 50 * 2) - 50;
        float px = c->s.clip.x + ox, py = c->s.clip.y + oy;
        if (box_can_place(px, py) && n < max) { out[n][0] = px; out[n][1] = py; n++; }
    }
    return n;
}

static void box_ai_continue(Weapon *w) {
    w->ai_delay = 40;
    if (box_can_place(w->ai_next_x, w->ai_next_y - 48) && frand() >= 0.4f) {
        w->ai_offset -= 48;
        w->ai_next_y -= 48;
        w->track_y -= 48;
        return;
    }
    for (int guard = 0; guard < 200; guard++) {
        if (++w->ai_index >= w->nai_list) {
            w->ai_index = 0;
            w->ai_offset -= 32;
        }
        float x = w->ai_list[w->ai_index][0], y = w->ai_list[w->ai_index][1] + w->ai_offset;
        if (box_can_place(x, y)) {
            w->ai_next_x = w->track_x = x;
            w->ai_next_y = w->track_y = y;
            w->track = true;
            return;
        }
    }
}

static void box_advance(Weapon *w) {
    if (++w->test_delay < 25) return;
    if (w->owner->team->ai && w->nai_list && w->owner->weapon == w && !w->finished) {
        if (w->ai_delay > 0) {
            if (--w->ai_delay < 1) {
                w->test_delay = 0;
                Weapon *b = w;
                while (b->next_box) b = b->next_box;
                weapon_place(b, w->ai_next_x, w->ai_next_y);
                clip_show(&b->s.clip);
                w->ai_delay_after = 10;
            }
        } else if (w->ai_delay_after > 0) {
            if (--w->ai_delay_after < 1) box_ai_continue(w);
        }
        w->track_x = w->ai_next_x;
        w->track_y = w->ai_next_y;
    }
    if (w->next_box) {
        box_advance(w->next_box);
        if (w->next_box->finished && !w->finished) w->finished = true;
    }
    if (w->mouse_down) {
        if (box_can_place(W.mouse_x, W.mouse_y)) {
            weapon_place(w, W.mouse_x, W.mouse_y);
            clip_show(&w->s.clip);
            weapon_update(w);
            W.mouse_down = false;
        }
        w->mouse_down = false;
    }
    if (!w->fired && !w->owner->team->ai && !w->mouse_down && W.mouse_down) w->mouse_down = true;
}

// ------------------------------------------------------------ otros comportamientos por cuadro
static void mine_explode(Weapon *w) {
    if (w->simulation) { w->sim_finished = true; return; }
    if (w->exploded) return;
    explosion_new(w->s.clip.x, w->s.clip.y, 250, 70, w->owner);
    sfx("pop");
    for (int i = W.nmines - 1; i >= 0; i--)
        if (W.mines[i] == w) {
            memmove(&W.mines[i], &W.mines[i + 1], sizeof W.mines[0] * (size_t)(W.nmines - i - 1));
            W.nmines--;
        }
    w->finished = true;
    w->exploded = true;
    weapon_destroy(w);
}

void pieces_next(Weapon *w, bool explode) {
    if (explode) {
        explosion_new(w->s.clip.x, w->s.clip.y, 50, 25, w->owner);
        sfx("pop");
    }
    if (++w->times_fired < 8) {
        w->fired = false;
        w->track = false;
        character_equip(w->owner, w->owner->weapon_index);
        W.pan_to = w->owner;
        if (w->owner->team->ai) w->ai_wait = 20;
        w->owner->weapon_locked = true;
    } else {
        w->finished = true;
        clip_hide(&w->s.clip);
    }
}

static void pieces_ai_continue(Weapon *w) {
    AIMove r[10];
    int n = weapon_random_throws(w, 10, r);
    AIMove *best = &r[0];
    float bs = -INFINITY;
    Team *mine = w->owner->team, *en = &W.teams[2 - w->owner->team->number];
    for (int j = 0; j < n; j++) {
        float s = 0;
        for (int i = 0; i < en->n; i++) {
            Character *c = en->chars[i];
            if (!c->alive) continue;
            float dx = r[j].ex - c->s.clip.x, dy = r[j].ey - c->s.clip.y, d2 = dx * dx + dy * dy;
            if (d2 < 4900) s += 1 - sqrtf(d2) / 70;
        }
        for (int i = 0; i < mine->n; i++) {
            Character *c = mine->chars[i];
            if (!c->alive) continue;
            float dx = r[j].ex - c->s.clip.x, dy = r[j].ey - c->s.clip.y, d2 = dx * dx + dy * dy;
            if (d2 < 4900) s -= 1.5f - sqrtf(d2) / 70;
        }
        if (s > bs) { bs = s; best = &r[j]; }
    }
    weapon_fire(w, best->vx, best->vy);
}

static void base_advance(Weapon *w) {
    weapon_motion(w);
    weapon_update(w);
    if (w->fired && !w->finished) {
        if (w->s.vy > 0 && w->s.clip.y > (float)(W.lh << 5)) w->finished = true;
        else if (stopped(w, 0.2f)) w->finished = true;
        if (!w->simulation) solid_splash_check(&w->s);
    }
    if (!w->fired && W.twanging == &w->s) solid_draw_twang_line(&w->s);
}

void seagull_end_shot(Weapon *g, Weapon *shot) {
    for (int i = g->nshots - 1; i >= 0; i--)
        if (g->shots[i] == shot) {
            memmove(&g->shots[i], &g->shots[i + 1], sizeof g->shots[0] * (size_t)(g->nshots - i - 1));
            g->nshots--;
        }
}

static void seagull_advance(Weapon *w) {
    if (!w->fired) {
        w->dotted.x = -mc_x(W.content);
        w->dotted.y = W.mouse_y;
        clip_update(&w->dotted);
        mc_set_visible(w->dotted.mc, W.mouse_visible);
    }
    base_advance(w);
    bool shoot = false;
    if (w->owner->team->ai) {
        if (w->nai_shot_x > 0 && w->s.clip.x >= w->ai_shot_x[0]) {
            memmove(w->ai_shot_x, w->ai_shot_x + 1, sizeof w->ai_shot_x[0] * (size_t)(w->nai_shot_x - 1));
            w->nai_shot_x--;
            shoot = true;
        }
    } else {
        shoot = W.mouse_down;
    }
    if (shoot) {
        if (!w->fired) {
            weapon_place(w, -300, W.mouse_y);
        } else if (!w->finished && w->s.clip.x < (float)(W.lw << 5) && w->nshots < 32) {
            Weapon *s = weapon_new(W_SEAGULLFIRE);
            s->s.clip.x = w->s.clip.x - 10;
            s->s.clip.y = w->s.clip.y;
            s->s.vx = w->s.vx;
            s->s.weight = 1;
            s->owner = w->owner;
            s->gull = w;
            s->s.hits_boxes = true;
            clip_show(&s->s.clip);
            w->shots[w->nshots++] = s;
            clip_hide(&w->s.clip);
            clip_show(&w->s.clip);
            mc_goto_label(w->s.clip.mc, "shot", true);
            char nm[8] = "poop1";
            nm[4] = (char)('1' + (int)(frand() * 3));
            sfx(nm);
        }
        W.mouse_down = false;
    }
    for (int i = 0; i < w->nshots; i++) {
        Weapon *s = w->shots[i];
        base_advance(s);
        if (s->s.clip.y > W.water.y && !s->finished) {
            seagull_end_shot(w, s);
            weapon_destroy(s);
            s->finished = true;
            i--;
        }
    }
    if (w->s.clip.x > (float)(W.lw << 5) + VIEW_CX && w->nshots < 1) w->finished = true;
    w->track_y = w->s.clip.y + 100;
}

float seagull_ai_simulation(Weapon *w, int *shots, int *nshots, float *height) {
    Team *mine = w->owner->team, *en = &W.teams[2 - w->owner->team->number];
    float top = INFINITY;
    for (int i = 0; i < en->n; i++)
        if (en->chars[i]->alive && en->chars[i]->s.clip.y < top) top = en->chars[i]->s.clip.y;
    top -= 100;
    top -= floorf(frand() * 100);
    int xs[10];
    for (int i = 0; i < 10; i++) xs[i] = (int)floorf(frand() * (float)(W.lw << 5));
    for (int i = 1; i < 10; i++)
        for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    float total = 0;
    int n = 0;
    for (int i = 0; i < 10; i++) {
        Weapon sim;
        memset(&sim, 0, sizeof sim);
        sim.type = W_SIMSHOT;
        sim.s.so = SO_WEAPON;
        sim.s.left = sim.s.right = sim.s.top = sim.s.bottom = 10;
        sim.s.weight = 1;
        sim.s.friction = 0.3f;
        sim.s.bounce = 0.2f;
        sim.s.hits_tiles = true;
        sim.s.hits_boxes = true;
        sim.s.clip.x = xs[i] - 10.0f;
        sim.s.clip.y = top;
        sim.s.vx = 10;
        int guard = 0;
        while (!sim.sim_finished && sim.s.clip.y < W.water.y && guard++ < 2000) solid_base_motion(&sim.s);
        float s = 0;
        for (int k = 0; k < en->n; k++) {
            Character *c = en->chars[k];
            if (!c->alive) continue;
            float dx = c->s.clip.x - sim.s.clip.x, dy = c->s.clip.y - sim.s.clip.y, d2 = dx * dx + dy * dy;
            if (d2 < 1600) s += 1 - sqrtf(d2) / 40;
        }
        for (int k = 0; k < mine->n; k++) {
            Character *c = mine->chars[k];
            if (!c->alive) continue;
            float dx = c->s.clip.x - sim.s.clip.x, dy = c->s.clip.y - sim.s.clip.y, d2 = dx * dx + dy * dy;
            if (d2 < 1600) s -= 1.5f - sqrtf(d2) / 40;
        }
        if (s > 0 && n < 16) {
            shots[n++] = xs[i];
            total += s;
        }
    }
    *nshots = n;
    *height = top;
    return total;
}

static void cannon_advance(Weapon *w) {
    Solid *s = &w->s;
    weapon_motion(w);
    if (w->ai_fire_time > 0) {
        if (--w->ai_fire_time < 1) weapon_fire(w, w->ai_fire.vx, w->ai_fire.vy);
    } else if (w->fired) {
        weapon_advance(w->cannonball);
        w->visibility -= 0.1f;
        if (w->visibility < 0) w->visibility = 0;
        if (w->visibility < 1) mc_set_alpha(s->clip.mc, w->visibility * 100);
        if (w->cannonball->finished && w->visibility == 0) {
            w->finished = true;
            clip_hide(&s->clip);
        }
    } else {
        MC *pin = mc_get(s->clip.mc, "pin");
        if (W.dragging == s) {
            float dx = s->clip.x - w->owner->s.clip.x, dy = s->clip.y - w->owner->s.clip.y + 100, d2 = dx * dx + dy * dy;
            if (d2 > 14400) {
                float d = sqrtf(d2);
                s->clip.x = w->owner->s.clip.x + dx * 120 / d;
                s->clip.y = w->owner->s.clip.y - 100 + dy * 120 / d;
            }
        }
        float lx = 0, ly = 0;
        if (s->clip.mc) mc_global_to_local(s->clip.mc, W.mouse_x + mc_x(W.content), W.mouse_y + mc_y(W.content), &lx, &ly);
        if (W.mouse_down && !w->dragging_pin && W.dragging != s) {
            float dx = lx - w->pin_x, dy = ly;
            if (dx * dx + dy * dy < 64) w->dragging_pin = true;
            else if (lx * lx + ly * ly < 400) W.dragging = s;
        }
        if (w->dragging_pin) {
            float dx = W.mouse_x - s->clip.x, dy = W.mouse_y - s->clip.y;
            float r = atan2f(dy, dx) * 180 / 3.14159265f + 180;
            r = fmodf(r, 360);
            s->clip.rotation = roundf(r);
            w->pin_x = lx;
            if (w->pin_x < -40) w->pin_x = -40;
            if (w->pin_x > -21) w->pin_x = -21;
            if (!W.mouse_down) {
                bool pulled = w->pin_x < -30;
                w->fire_strength = pulled ? 30 : 0;
                w->dragging_pin = false;
                if (pulled) w->owner->can_shoot = w->owner->can_throw = false;
            }
        } else {
            w->pin_x += 15;
            if (w->pin_x >= -21) {
                w->pin_x = -21;
                if (w->fire_strength > 4) {
                    int rot = ((int)s->clip.rotation % 360 + 360) % 360;
                    weapon_fire(w, w->fire_strength * trig_cos(rot), w->fire_strength * trig_sin(rot));
                }
            }
        }
        if (pin) mc_set_x(pin, w->pin_x);
    }
    weapon_update(w);
}

void weapon_advance(Weapon *w) {
    Solid *s = &w->s;
    switch (w->type) {
    case W_CHERRYBOMB:
    case W_DYNAMITE:
        base_advance(w);
        smoke(w);
        return;
    case W_RUMBOTTLE:
        if (!w->simulation && w->fired) {
            s->clip.rotation += s->vx * 2;
            weapon_update(w);
        }
        base_advance(w);
        smoke(w);
        return;
    case W_PARACHUTEBOMB:
        base_advance(w);
        if (!w->simulation && !w->finished && !w->chute_open) debris_new(W.effects, "cannonSmokeTrail", s->clip.x, s->clip.y);
        return;
    case W_MINE:
        base_advance(w);
        if (w->active && w->nbeeps > 0 && 60 - w->countdown >= w->beeps[0]) {
            sfx("mine_beep");
            memmove(w->beeps, w->beeps + 1, sizeof w->beeps[0] * (size_t)(w->nbeeps - 1));
            w->nbeeps--;
        }
        return;
    case W_ANCHOR:
        if (++w->test_delay < 25) return;
        if (w->ai_wait > 0 && w->owner->team->ai) {
            w->ai_wait--;
            w->test_delay = 0;
            return;
        }
        if (!w->fired && W.mouse_down) {
            weapon_place(w, W.mouse_x, W.mouse_y);
            clip_show(&s->clip);
            w->owner->can_shoot = w->owner->can_throw = false;
        }
        if (w->fired && !w->hit_bottom) {
            s->vy = 40;
            weapon_motion(w);
            weapon_update(w);
        }
        if (w->hit_bottom) {
            if (w->hold_time > 0) w->hold_time--;
            else if (w->fade_time > 0) {
                w->fade_time--;
                whiteout(s->clip.mc, w->fade_time / 10.0f);
            } else {
                clip_hide(&s->clip);
                w->finished = true;
            }
        }
        return;
    case W_WOODENCRATE:
    case W_GUNPOWDERBARREL:
        box_advance(w);
        return;
    case W_CANNON:
        cannon_advance(w);
        return;
    case W_CANNONBALL:
        weapon_motion(w);
        weapon_update(w);
        smoke(w);
        if (!w->finished)
            for (int t = 0; t < 2 && !w->finished; t++)
                for (int i = 0; i < W.teams[t].n; i++) {
                    Character *c = W.teams[t].chars[i];
                    if (!c->alive || c == w->owner) continue;
                    if (c->s.clip.x - c->s.left <= s->clip.x + s->right && c->s.clip.x + c->s.right >= s->clip.x - s->left &&
                        c->s.clip.y - c->s.top <= s->clip.y + s->bottom && c->s.clip.y + c->s.bottom >= s->clip.y - s->top) {
                        w->finished = true;
                        clip_hide(&s->clip);
                        explosion_new(s->clip.x, s->clip.y, 100, 50, w->owner);
                        s->vx = s->vy = 0;
                        break;
                    }
                }
        return;
    case W_PIECESOFEIGHT:
        if (W.twanging != s && !w->fired) {
            s->clip.x = w->owner->s.clip.x;
            s->clip.y = w->owner->s.clip.y + 5;
            s->vx = s->vy = 0;
        }
        if (w->ai_wait > 0 && --w->ai_wait < 1) pieces_ai_continue(w);
        base_advance(w);
        if (s->clip.y >= W.water.y && !w->finished) pieces_next(w, false);
        return;
    case W_SEAGULL:
        seagull_advance(w);
        return;
    case W_TIDALWAVE:
        if (w->fired) {
            base_advance(w);
            if (s->clip.x > (float)(W.lw << 5) + 550) {
                w->finished = true;
                clip_hide(&s->clip);
            }
            for (int t = 0; t < 2; t++)
                for (int i = 0; i < W.teams[t].n; i++) {
                    Character *c = W.teams[t].chars[i];
                    if (c->alive && c->s.clip.y >= W.water.y - 300 && c->s.clip.x >= s->clip.x - 150 && c->s.clip.x <= s->clip.x + 150)
                        character_subtract_health(c, 5);
                }
        } else if (W.mouse_down) {
            extern void tidal_start(Weapon *w);
            tidal_start(w);
        }
        return;
    case W_VOODOODOLL:
        if (w->fired && !w->simulation) s->clip.rotation += s->vx * 2;
        weapon_motion(w);
        weapon_update(w);
        if (w->fired) {
            if (w->frames_on_this > 0) {
                if (--w->frames_on_this <= 0) {
                    W.pan_to = w->target;
                    w->track = false;
                }
            } else if (W.pan_to != w->target) {
                if (w->frames_on_target > 0) w->frames_on_target--;
                else if (!w->thrown_character) {
                    if (w->target) {
                        w->target->s.vx = w->throw_vx;
                        w->target->s.vy = w->throw_vy;
                    }
                    w->thrown_character = true;
                } else {
                    mc_set_alpha(s->clip.mc, mc_alpha(s->clip.mc) - 10);
                    if (mc_alpha(s->clip.mc) <= 0) w->finished = true;
                }
            }
        }
        if (!w->fired && W.twanging == s) solid_draw_twang_line(s);
        return;
    default:
        base_advance(w);
        return;
    }
}

void tidal_start(Weapon *w) {
    w->fired = true;
    w->track = true;
    w->s.clip.x = -550;
    w->s.clip.y = W.water.y;
    w->s.vx = 20;
    clip_show(&w->s.clip);
    char lab[8];
    snprintf(lab, sizeof lab, "anim%d", W.sky);
    mc_goto_label(w->s.clip.mc, lab, true);
    w->owner->can_throw = w->owner->can_shoot = false;
}

void voodoo_set_target(Weapon *w, Character *c) {
    if (w->fired) return;
    w->target = c;
    w->s.twangable = true;
    W.pan_to = w->owner;
}

// ------------------------------------------------------------ simulaciones para la IA
int weapon_random_throws(Weapon *w, int count, AIMove *out) {
    Solid *s = &w->s;
    if (w->type == W_ANCHOR) {
        int n = 0;
        w->simulation = true;
        for (int i = 0; i < count; i++) {
            s->clip.x = floorf(frand() * (float)(W.lw << 5));
            s->clip.y = -200;
            s->vx = 0;
            s->vy = 40;
            w->sim_finished = false;
            int guard = 0;
            while (!w->sim_finished && s->clip.y < W.water.y && guard++ < 2000) weapon_motion(w);
            if (w->sim_finished) {
                memset(&out[n], 0, sizeof out[n]);
                out[n].x = out[n].ex = s->clip.x;
                out[n].ey = s->clip.y;
                n++;
            }
        }
        w->simulation = false;
        return n;
    }
    if (w->type == W_CANNON) {
        for (int i = 0; i < count; i++) {
            int a = (int)floorf(frand() * 360);
            float r = frand() * w->drag_range / 2;
            float sx = w->owner->s.clip.x + trig_cos(a) * r, sy = w->owner->s.clip.y + w->drag_offset + trig_sin(a) * r;
            s->vx = sx - s->clip.x;
            s->vy = sy - s->clip.y;
            weapon_motion(w);
            s->vx = s->vy = 0;
            a = (int)floorf(frand() * 360);
            Weapon *b = weapon_new(W_CANNONBALL);
            clip_hide(&b->s.clip);
            b->s.clip.x = s->clip.x;
            b->s.clip.y = s->clip.y;
            b->simulation = true;
            weapon_fire(b, 30 * trig_cos(a), 30 * trig_sin(a));
            int guard = 0;
            while (!b->sim_finished && guard++ < 2000) weapon_motion(b);
            memset(&out[i], 0, sizeof out[i]);
            out[i].sx = sx;
            out[i].sy = sy;
            out[i].vx = 30 * trig_cos(a);
            out[i].vy = 30 * trig_sin(a);
            out[i].ex = b->s.clip.x;
            out[i].ey = b->s.clip.y;
            out[i].angle = a;
            weapon_destroy(b);
        }
        return count;
    }
    float ox = s->clip.x, oy = s->clip.y;
    w->simulation = true;
    for (int i = 0; i < count; i++) {
        int a = 180 + (int)(frand() * 180);
        float f = 5 + frand() * (s->twang_max - 5);
        float vx = trig_cos(a) * f, vy = trig_sin(a) * f;
        s->clip.x = ox;
        s->clip.y = oy;
        s->vx = vx;
        s->vy = vy;
        w->sim_finished = false;
        for (int k = 0; !w->sim_finished && k <= 100; k++) weapon_motion(w);
        memset(&out[i], 0, sizeof out[i]);
        out[i].vx = vx;
        out[i].vy = vy;
        out[i].ex = s->clip.x;
        out[i].ey = s->clip.y;
    }
    s->clip.x = ox;
    s->clip.y = oy;
    s->vx = s->vy = 0;
    w->simulation = false;
    return count;
}

void weapon_ai_perform(Weapon *w, AIMove *m) {
    Solid *s = &w->s;
    switch (w->type) {
    case W_ANCHOR:
        weapon_place(w, m->x, 0);
        w->ai_wait = 20;
        clip_show(&s->clip);
        return;
    case W_WOODENCRATE:
    case W_GUNPOWDERBARREL:
        w->nai_list = m->nposs < 3 ? m->nposs : 3;
        for (int i = 0; i < w->nai_list; i++) { w->ai_list[i][0] = m->poss[i][0]; w->ai_list[i][1] = m->poss[i][1]; }
        box_ai_continue(w);
        return;
    case W_CANNON:
        s->vx = m->sx - s->clip.x;
        s->vy = m->sy - s->clip.y;
        weapon_motion(w);
        s->vx = s->vy = 0;
        s->clip.rotation = (float)m->angle;
        w->ai_fire = *m;
        w->ai_fire_time = 25;
        return;
    case W_SEAGULL:
        weapon_place(w, -300, m->height);
        w->nai_shot_x = m->nshots;
        memcpy(w->ai_shot_x, m->shots, sizeof w->ai_shot_x[0] * (size_t)m->nshots);
        return;
    case W_TIDALWAVE:
        tidal_start(w);
        return;
    case W_VOODOODOLL:
        w->fired = true;
        s->vx = w->throw_vx = m->vx;
        s->vy = w->throw_vy = m->vy;
        w->target = m->target;
        sfx("voodoo");
        return;
    default:
        weapon_fire(w, m->vx, m->vy);
        return;
    }
}
