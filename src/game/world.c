// Controller, TileSystem, Clip, Water, efectos, cofres y bocadillos.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../core/audio.h"
#include "game.h"
#include "lines.h"

World W;

const char *const weapon_names[W_COUNT] = {
    "", "cherryBomb", "dynamite", "boulder", "piecesOfEight", "rumBottle", "banana", "parachuteBomb",
    "woodenCrate", "gunpowderBarrel", "seagull", "mine", "cannon", "anchor", "voodooDoll", "tidalWave",
    "cannonball", "cannonball", "seagullFire", "",
};

WType weapon_from_name(const char *n) {
    for (int i = 1; i <= W_CANNONBALL_ITEM; i++)
        if (!strcmp(weapon_names[i], n)) return (WType)i;
    return W_NONE;
}

float frand(void) { return (float)((double)rand() / ((double)RAND_MAX + 1.0)); }

void *world_alloc(size_t size) {
    void *p = calloc(1, size);
    if (W.nalloc == W.capalloc) {
        W.capalloc = W.capalloc ? W.capalloc * 2 : 256;
        W.alloc = realloc(W.alloc, sizeof *W.alloc * W.capalloc);
    }
    W.alloc[W.nalloc++] = p;
    return p;
}

// ------------------------------------------------------------ Clip
void clip_init(Clip *c, MC *parent, const char *link, int kind, void *owner) {
    memset(c, 0, sizeof *c);
    c->parent = parent;
    if (link) snprintf(c->link, sizeof c->link, "%s", link);
    c->kind = kind;
    c->owner = owner;
}

static inline float trunc0(float v) { return (float)(int)v; }  // "<< 0" de AS

void clip_update(Clip *c) {
    if (c->holder) {
        mc_set_xy(c->holder, trunc0(c->x), trunc0(c->y));
        if (c->mc) mc_set_rotation(c->mc, c->rotation);
    } else if (c->mc) {
        mc_set_xy(c->mc, trunc0(c->x + 0.5f), trunc0(c->y + 0.5f));
        mc_set_rotation(c->mc, c->rotation);
    }
}

void clip_show(Clip *c) {
    if (c->mc || c->holder || !c->parent) return;
    int d = mc_next_depth(c->parent);
    if (c->use_holder) {
        c->holder = mc_create_empty(c->parent, "holder", d);
        c->holder->cl = c;
        c->mc = mc_attach(c->holder, c->link, c->link, 0);
    } else {
        c->mc = mc_attach(c->parent, c->link, c->link, d);
    }
    if (c->mc) c->mc->cl = c;
    clip_update(c);
}

void clip_hide(Clip *c) {
    if (c->holder) mc_remove(c->holder);
    else if (c->mc) mc_remove(c->mc);
    c->mc = NULL;
    c->holder = NULL;
}

void clip_destroy(Clip *c) {
    clip_hide(c);
    c->parent = NULL;
}

// ------------------------------------------------------------ sonidos (SfxManager)
static MC *sfx_root;
extern bool app_sfx_on(void);

void sfx(const char *name) {
    if (!app_sfx_on()) return;
    if (!sfx_root) {
        sfx_root = mc_new_root();
        mc_attach(sfx_root, "sfx_manager", "sfx_manager", 0);
    }
    MC *m = mc_get(mc_get(sfx_root, "sfx_manager"), name);
    if (m) mc_goto(m, 2, true);
}

void sfx_tick(void) { if (sfx_root) mc_tick(sfx_root); }

// ------------------------------------------------------------ efectos
typedef struct { Clip clip; float radius, max_damage; Character *caster; } Explosion;

void explosion_new(float x, float y, float size, float max_damage, Character *caster) {
    Explosion *e = calloc(1, sizeof *e);
    clip_init(&e->clip, W.content, "explosion", CK_EXPLOSION, e);
    e->clip.x = x;
    e->clip.y = y;
    clip_show(&e->clip);
    if (e->clip.mc) { mc_set_xscale(e->clip.mc, size); mc_set_yscale(e->clip.mc, size); }
    e->radius = size / 2 + 20;
    e->max_damage = max_damage;
    e->caster = caster;
}

static void explosion_hit(Explosion *e) {
    float r2 = e->radius * e->radius;
    for (int t = 0; t < 2; t++) {
        Team *tm = &W.teams[t];
        for (int i = 0; i < tm->n; i++) {
            Character *c = tm->chars[i];
            if (!c->alive) continue;
            float dx = c->s.clip.x - e->clip.x, dy = c->s.clip.y - e->clip.y, d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            float d = sqrtf(d2);
            if (d > 0) { dx /= d; dy /= d; } else { dx = dy = NAN; }
            float k = 1 - d / e->radius, f = 0.06f * k * e->max_damage;
            if (d > 0) {
                c->s.vx += dx * 5 * f;
                c->s.vy += dy * 5 * f;
            }
            c->s.vy -= f * 6;
            character_subtract_health(c, e->max_damage * k);
            if (e->caster) e->caster->evilness += k;
            c->hit = true;
        }
    }
    for (int i = W.nboxes - 1; i >= 0; i--) {
        if (i >= W.nboxes) continue;
        Weapon *b = W.boxes[i];
        float cx = e->clip.x, cy = e->clip.y;
        if (cx < b->s.clip.x - b->s.left) cx = b->s.clip.x - b->s.left;
        if (cx > b->s.clip.x + b->s.right) cx = b->s.clip.x + b->s.right;
        if (cy < b->s.clip.y - b->s.top) cy = b->s.clip.y - b->s.top;
        if (cy > b->s.clip.y + b->s.bottom) cy = b->s.clip.y + b->s.bottom;
        float dx = e->clip.x - cx, dy = e->clip.y - cy;
        if (dx * dx + dy * dy <= r2) box_explode(b);
    }
}

void debris_new(MC *layer, const char *link, float x, float y) {
    Clip *c = calloc(1, sizeof *c);
    clip_init(c, layer, link, CK_DEBRIS, c);
    c->x = x;
    c->y = y;
    clip_show(c);
}

typedef struct { Clip clip; bool to_right; } Flame;

void flame_new(float x, float y, bool to_right) {
    Flame *f = calloc(1, sizeof *f);
    clip_init(&f->clip, W.characters, "sweepingFlame", CK_FLAME, f);
    f->clip.x = x;
    f->clip.y = y;
    f->to_right = to_right;
    clip_show(&f->clip);
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < W.teams[t].n; i++) {
            Character *c = W.teams[t].chars[i];
            if (!c->alive) continue;
            float dx = c->s.clip.x - x, dy = c->s.clip.y + c->s.bottom - y;
            if (dx * dx + dy * dy < 64) {
                c->s.vx = (frand() - 0.5f) * 8;
                c->s.vy = -(frand() * 2 + 6);
                character_subtract_health(c, 30);
            }
        }
}

static void flame_next(Flame *f) {
    float x = f->clip.x + 8 * (f->to_right ? 1 : -1);
    int gx = (int)x >> 5, gy = (int)f->clip.y >> 5;
    if (!tile_at(gx, gy) || tile_at(gx, gy - 1)) return;
    flame_new(x, f->clip.y, f->to_right);
    W.inactivity = 0;
}

static void chest_destroy(Chest *c) {
    clip_destroy(&c->clip);
    for (int i = W.nchests - 1; i >= 0; i--)
        if (W.chests[i] == c) {
            memmove(&W.chests[i], &W.chests[i + 1], sizeof W.chests[0] * (W.nchests - i - 1));
            W.nchests--;
        }
}

void world_clip_call(MC *mc, const char *stmt) {
    Clip *c = mc ? mc->cl : NULL;
    if (!c) return;
    if (!strcmp(stmt, "cl.destroy()")) {
        switch (c->kind) {
        case CK_DEBRIS: clip_destroy(c); free(c); break;
        case CK_EXPLOSION: clip_destroy(c); free(c->owner); break;
        case CK_FLAME: clip_destroy(c); free(c->owner); break;
        case CK_CHEST: chest_destroy(c->owner); break;
        case CK_SOLID: weapon_destroy(c->owner); break;
        default: clip_destroy(c); break;
        }
    } else if (!strcmp(stmt, "cl.hit()") && c->kind == CK_EXPLOSION) {
        explosion_hit(c->owner);
    } else if (!strcmp(stmt, "cl.createNext()") && c->kind == CK_FLAME) {
        flame_next(c->owner);
    }
}

// ------------------------------------------------------------ cofres
Chest *chest_falling(void) {
    for (int i = 0; i < W.nchests; i++)
        if (W.chests[i]->falling) return W.chests[i];
    return NULL;
}

static Chest *chest_new(float x, float floor_y) {
    Chest *c = world_alloc(sizeof *c);
    clip_init(&c->clip, W.objects, "treasureChest", CK_CHEST, c);
    c->falling = true;
    c->visibility = 1;
    int n = 1 + (int)(frand() * 3);
    if (W.game_type != 2)  // en dos jugadores se decide al abrirlo (chest_roll_2p)
        for (int i = 0; i < n && W.npotential > 0; i++) {
            c->counts[c->ncontains] = 1;
            c->contains[c->ncontains++] = W.potential[(int)(frand() * W.npotential)];
        }
    c->clip.x = x;
    c->clip.y = -300;
    c->floor_y = floor_y;
    clip_show(&c->clip);
    mc_goto_label(c->clip.mc, "falling", false);
    sfx("chest_appear");
    return c;
}

void chest_drop_new(void) {
    if (W.nchests >= W.max_chests || W.ndrop == 0 || W.nchests >= 8) return;
    int col = W.drop_cols[(int)(frand() * W.ndrop)];
    int y;
    for (y = 0; y < W.lh; y++)
        if (tile_at(col, y)) break;
    if (y >= W.lh) return;
    float fy = (float)(y << 5), x = (float)((col << 5) + 16);
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < W.teams[t].n; i++) {
            Character *c = W.teams[t].chars[i];
            if (c->alive && c->s.clip.x >= x - 32 && c->s.clip.x <= x + 32 && c->s.clip.y <= fy + 32) return;
        }
    for (int i = 0; i < W.nchests; i++)
        if (W.chests[i]->clip.x == x) return;
    for (int i = 0; i < W.nboxes; i++)
        if (fabsf(W.boxes[i]->s.clip.x - x) < 32) return;
    W.chests[W.nchests++] = chest_new(x, fy);
}

extern const char *weapon_title(WType w);

// Dos jugadores: entre 1 y 5 armas distintas de cualquier tipo, entre 1 y 10 de cada una.
// Nunca las que ya son infinitas para el pirata que lo abre (cherry bomb, cañón según el mapa).
static void chest_roll_2p(Chest *c, Character *ch) {
    WType pool[W_TIDALWAVE + 1];
    int np = 0;
    for (int w = W_CHERRYBOMB; w <= W_TIDALWAVE; w++)
        if (!character_weapon_infinite(ch, (WType)w)) pool[np++] = (WType)w;
    int n = 1 + (int)(frand() * 5);
    c->ncontains = 0;
    for (int i = 0; i < n && np > 0; i++) {
        int k = (int)(frand() * np);
        c->contains[c->ncontains] = pool[k];
        c->counts[c->ncontains++] = 1 + (int)(frand() * 10);
        pool[k] = pool[--np];
    }
}

void chest_advance(Chest *c) {
    if (c->finished) {
        c->visibility -= 0.1f;
    } else if (c->falling) {
        c->clip.y += 3;
        if (c->clip.y >= c->floor_y - 15) {
            c->clip.y = c->floor_y - 15;
            c->falling = false;
            mc_goto_label(c->clip.mc, "touchdown", true);
        }
        clip_update(&c->clip);
    } else if (c->touched) {
        if (--c->to_next <= 0) {
            if (c->ncontains > 0) {
                c->to_next = 40;
                mc_goto_label(c->clip.mc, "weapon_out", true);
                c->released = c->contains[0];
                int cnt = c->counts[0];
                memmove(c->contains, c->contains + 1, sizeof c->contains[0] * (c->ncontains - 1));
                memmove(c->counts, c->counts + 1, sizeof c->counts[0] * (c->ncontains - 1));
                c->ncontains--;
                for (int k = 0; k < cnt && c->touched->nhas < MAX_WEAPONS; k++) c->touched->has[c->touched->nhas++] = c->released;
                char buf[96];
                if (cnt > 1) snprintf(buf, sizeof buf, "collected %s x%d", weapon_title(c->released), cnt);
                else snprintf(buf, sizeof buf, "collected %s", weapon_title(c->released));
                world_say(buf);
                sfx("icon_collect");
            } else {
                mc_goto_label(c->clip.mc, "fade_out", true);
                c->finished = true;
            }
        }
        if (c->clip.mc) {
            MC *wm = mc_get(c->clip.mc, "weapon");
            mc_goto_label(wm, c->touched->team->number != 1 ? "blue" : "red", false);
            if (c->released) mc_goto_label(mc_get(wm, "weapon"), weapon_names[c->released], false);
            if (!c->touched->alive) {
                mc_set_alpha(c->clip.mc, mc_alpha(c->clip.mc) - 5);
                c->visibility -= 0.05f;
            }
        }
    } else {
        for (int t = 0; t < 2 && !c->touched; t++)
            for (int i = 0; i < W.teams[t].n; i++) {
                Character *ch = W.teams[t].chars[i];
                if (!ch->alive || W.dragging == &ch->s) continue;
                if (ch->s.clip.x + ch->s.right >= c->clip.x - 20 && ch->s.clip.x - ch->s.left <= c->clip.x + 20 &&
                    ch->s.clip.y + ch->s.bottom >= c->clip.y - 16 && ch->s.clip.y - ch->s.top <= c->clip.y + 16) {
                    c->touched = ch;
                    break;
                }
            }
        if (c->touched) {
            if (W.game_type == 2) chest_roll_2p(c, c->touched);
            mc_goto_label(c->clip.mc, "open", true);
            c->to_next = 10;
            sfx("click");
        }
    }
    if ((c->falling || c->touched) && !c->finished) W.inactivity = 0;
    c->time_taken++;
}

// ------------------------------------------------------------ bocadillos
Speech *speech_new(const char *text, Character *target) {
    Speech *s = world_alloc(sizeof *s);
    clip_init(&s->clip, W.characters, "speechBubble", CK_SPEECH, s);
    snprintf(s->text, sizeof s->text, "%s", text);
    s->start_time = 10;
    sfx(team_type(target->team));
    s->target = target;
    s->clip.x = target->s.clip.x;
    s->clip.y = target->s.clip.y - 90;
    return s;
}

static MC *speech_field(Speech *s) { return mc_path(s->clip.mc, "textHolder.textField"); }

void speech_advance(Speech *s) {
    if (s->start_time > 0) {
        if (--s->start_time < 1) {
            clip_show(&s->clip);
            // El bocadillo no debe salirse de la pantalla de 400 px
            mc_set_text(speech_field(s), "");
        }
        return;
    }
    int len = (int)strlen(s->text);
    if (s->shown < len) {
        s->shown += 3;
        if (s->shown > len) s->shown = len;
        char buf[256];
        memcpy(buf, s->text, (size_t)s->shown);
        buf[s->shown] = 0;
        mc_set_text(speech_field(s), buf);
        s->complete_time = 0;
    } else {
        s->complete_time++;
    }
}

void speech_skip(Speech *s) {
    if (s->complete_time > 1) s->complete_time = 1000000;
    else {
        s->shown = (int)strlen(s->text);
        mc_set_text(speech_field(s), s->text);
    }
}

void speech_destroy(Speech *s) { clip_destroy(&s->clip); }

// ------------------------------------------------------------ texto de turno (IngameTextArea)
void world_say(const char *s) {
    if (W.nlines >= 8) return;
    snprintf(W.lines[W.nlines++], sizeof W.lines[0], "%s", s);
    if (W.nlines == 1) {
        MC *tf = mc_get(W.text_area, "textField");
        mc_set_text(tf, s);
    }
}

static void text_area_tick(void) {
    if (W.speech || !W.text_area) return;
    if (W.nlines > 0) {
        if (++W.line_frame > 70) {
            W.line_frame = 0;
            memmove(W.lines[0], W.lines[1], sizeof W.lines[0] * (W.nlines - 1));
            W.nlines--;
            mc_set_text(mc_get(W.text_area, "textField"), W.nlines ? W.lines[0] : "");
        }
        float base = VIEW_H;  // el original sube 30 px desde el borde inferior (400 -> 370)
        float y;
        if (W.line_frame < 10) y = base - 3 * W.line_frame;
        else if (W.line_frame < 60) y = base - 30;
        else y = base - 30 + 3 * (W.line_frame - 60);
        mc_set_y(W.text_area, y);
    }
}

// ------------------------------------------------------------ TileSystem
static void tile_set_visible(int x, int y, bool v, bool bg) {
    int i = y * W.lw + x;
    uint16_t t = bg ? W.bggrid[i] : W.grid[i];
    MC **slot = bg ? &W.bgtile_mc[i] : &W.tile_mc[i];
    if (!t || t >= 0xFFFE) return;
    if (v && !*slot) {
        MC *layer = bg ? W.back_tiles : W.tiles;
        MC *m = mc_attach_id(layer, t, "tile", mc_next_depth(layer));
        if (!m) return;
        mc_set_xy(m, (float)(x << 5), (float)(y << 5));
        if (!bg && m->nframes > 1) mc_goto(m, 1 + W.anim_counter % m->nframes, true);
        *slot = m;
    } else if (!v && *slot) {
        mc_remove(*slot);
        *slot = NULL;
    }
}

void tiles_pan(float nx, float ny, bool force) {
    if (nx < -VIEW_CX) nx = -VIEW_CX;
    if (nx > -VIEW_CX + (W.lw << 5)) nx = (float)(-VIEW_CX + (W.lw << 5));
    if (ny < -VIEW_CY) ny = -VIEW_CY;
    if (ny > -VIEW_CY + (W.lh << 5)) ny = (float)(-VIEW_CY + (W.lh << 5));
    if (ny > W.water_y - WATER_MARGIN) ny = W.water_y - WATER_MARGIN;
    if (nx == W.cam_x && ny == W.cam_y && !force) return;
    int ox = (int)W.cam_x >> 5, oy = (int)W.cam_y >> 5, gx = (int)nx >> 5, gy = (int)ny >> 5;
    if (ox != gx || oy != gy || force) {
        int cols = VIEW_W / 32 + 1, rows = VIEW_H / 32 + 1;
        int ox2 = ox + cols, oy2 = oy + rows, gx2 = gx + cols, gy2 = gy + rows;
        ox--; oy--; gx--; gy--;
        int x0 = ox < gx ? ox : gx, x1 = ox2 > gx2 ? ox2 : gx2, y0 = oy < gy ? oy : gy, y1 = oy2 > gy2 ? oy2 : gy2;
        if (force) { x0 = 0; x1 = W.lw - 1; y0 = 0; y1 = W.lh - 1; }
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > W.lw - 1) x1 = W.lw - 1;
        if (y1 > W.lh - 1) y1 = W.lh - 1;
        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++) {
                bool v = x >= gx && x <= gx2 && y >= gy && y <= gy2;
                tile_set_visible(x, y, v, false);
                tile_set_visible(x, y, v, true);
            }
    }
    W.cam_x = nx;
    W.cam_y = ny;
    mc_set_xy(W.content, -roundf(nx), -roundf(ny));
}

float tiles_pan_towards(float tx, float ty, float step) {
    float dx = tx - W.cam_x, dy = ty - W.cam_y, d = sqrtf(dx * dx + dy * dy);
    if (d <= step) { tiles_pan(tx, ty, false); return 0; }
    tiles_pan(W.cam_x + step * dx / d, W.cam_y + step * dy / d, false);
    return d - step;
}

static float slide(float from, float to, float by) {
    if (from < to) { from += by; return from > to ? to : from; }
    from -= by;
    return from < to ? to : from;
}

// Scroll manual (cruceta/circle pad) o paneo automático
extern void hud_scroll_input(int *dx, int *dy);

static void tiles_scrolling(void) {
    if (W.dragging) return;
    W.scroll_dx = W.scroll_dy = 0;
    Chest *ch = chest_falling();
    if (W.speech) { tiles_pan_towards(W.speech->clip.x - VIEW_CX, W.speech->clip.y + 20 - VIEW_CY, 50); return; }
    if (W.popup) return;
    if (W.current->ai && !W.current->ai_finished) return;
    if (ch && ch->time_taken < 100) { tiles_pan_towards(ch->clip.x - VIEW_CX, ch->clip.y - VIEW_CY, 50); return; }
    Character *sel = W.current->selected;
    if (sel && sel->thrown && !sel->throw_finished) {
        tiles_pan_towards(sel->s.clip.x - VIEW_CX, sel->s.clip.y - 30 - VIEW_CY, 30);
        return;
    }
    if (sel && sel->weapon && !sel->weapon->finished && sel->weapon->track) {
        tiles_pan_towards(sel->weapon->track_x - VIEW_CX, sel->weapon->track_y - 30 - VIEW_CY, 30);
        return;
    }
    if (W.pan_to) {
        if (tiles_pan_towards(W.pan_to->s.clip.x - VIEW_CX, W.pan_to->s.clip.y - 30 - VIEW_CY, 30) == 0) W.pan_to = NULL;
        return;
    }
    hud_scroll_input(&W.scroll_dx, &W.scroll_dy);
    W.scroll_x = slide(W.scroll_x, W.scroll_dx * 10.0f, 1);
    W.scroll_y = slide(W.scroll_y, W.scroll_dy * 10.0f, 1);
    tiles_pan(W.cam_x + W.scroll_x, W.cam_y + W.scroll_y, false);
}

static float range_alpha;

static void tiles_advance(void) {
    W.anim_counter++;
    tiles_scrolling();
    Character *sel = W.current->selected;
    Weapon *eq = sel ? sel->weapon : NULL;
    if (eq && eq->type == W_CANNON && !eq->fired) {
        if (range_alpha < 100) range_alpha += 20;
        mc_set_xy(W.range_circle, sel->s.clip.x, sel->s.clip.y + eq->drag_offset);
        mc_set_xscale(W.range_circle, eq->drag_range);
        mc_set_yscale(W.range_circle, eq->drag_range);
    } else if (range_alpha > 0) {
        range_alpha -= 20;
    }
    mc_set_alpha(W.range_circle, range_alpha);
    mc_set_visible(W.range_circle, range_alpha > 0);
    // Objetivo del muñeco vudú / personaje bajo el cursor
    bool voodoo = eq && eq->type == W_VOODOODOLL;
    W.hover = NULL;
    if ((!sel || voodoo) && !W.current->ai) {
        Team *tm = voodoo ? &W.teams[2 - W.current->number] : W.current;
        Character *best = voodoo ? eq->target : NULL;
        float bd = 900;
        if (voodoo && eq->fired) best = eq->target;
        for (int i = 0; i < tm->n; i++) {
            Character *c = tm->chars[i];
            float dx = c->s.clip.x - W.mouse_x, dy = c->s.clip.y - W.mouse_y, d = dx * dx + dy * dy;
            if (d < bd && W.mouse_visible) { bd = d; best = c; }
        }
        W.hover = best;
    }
}

// Pulsación del "ratón" virtual (TileSystem.mouseDown/mouseUp)
void tiles_mouse_down(void) {
    if (W.speech) { speech_skip(W.speech); return; }
    W.mouse_down = true;
    if (W.current->ai) return;
    Character *sel = W.current->selected;
    if (!sel) {
        Character *best = NULL;
        float bd = 900;
        for (int i = 0; i < W.current->n; i++) {
            Character *c = W.current->chars[i];
            float dx = c->s.clip.x - W.mouse_x, dy = c->s.clip.y - W.mouse_y, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = c; }
        }
        if (best) team_select(W.current, best, false);
    }
}

void tiles_mouse_up(void) {
    W.mouse_down = false;
    if (W.dragging) {
        solid_release(W.dragging);
        W.dragging = NULL;
    }
    if (W.twanging) {
        Solid *s = W.twanging;
        W.twanging = NULL;
        if (s->so == SO_CHARACTER) character_twang((Character *)s);
        else weapon_twang((Weapon *)s);
    }
}

// ------------------------------------------------------------ Water
static void water_advance(void) {
    if (!W.has_water) return;
    float cx = mc_x(W.content), cy = mc_y(W.content);
    W.water.x = -49 * ceilf(cx / 49);
    clip_update(&W.water);
    MC *bg = W.background;
    float wy = W.water.y + cy - BG_SHIFT;  // posición del agua en coordenadas del fondo original
    mc_set_y(mc_get(bg, "waterBackground"), wy);
    float k = wy - 350;
    mc_set_y(mc_get(bg, "cloudBase"), floorf(k * 0.5f));
    mc_set_y(mc_get(bg, "hills"), floorf(k * 0.3f + 45));
    mc_set_y(mc_get(bg, "frontClouds"), floorf(k * 0.4f - 30));
    mc_set_y(mc_get(bg, "backClouds"), floorf(k * 0.2f - 30));
    float nm;
#define NEGMOD(a, b) (nm = fmodf((a), (b)), (a) >= 0 ? nm - (b) : nm)
    mc_set_x(mc_get(bg, "cloudBase"), floorf(NEGMOD(cx * 0.3f, 550)));
    mc_set_x(mc_get(bg, "hills"), floorf(NEGMOD(cx * 0.25f, 840)));
    mc_set_x(mc_get(bg, "frontClouds"), floorf(NEGMOD(cx * 0.3f, 1000)));
    mc_set_x(mc_get(bg, "backClouds"), floorf(NEGMOD(cx * 0.2f, 900)) - 450);
    if (W.water.mc) mc_set_visible(W.water.mc, W.cam_y > W.water.y - 420);
}

void world_set_sky(int colour) {
    W.sky = colour;
    char lab[32];
    snprintf(lab, sizeof lab, "water%d", colour);
    mc_goto_label(W.water.mc, lab, false);
    snprintf(lab, sizeof lab, "background%d", colour);
    mc_goto_label(W.background, lab, false);
}

void solid_splash_check(Solid *s) {
    bool over = s->clip.y < W.water.y;
    if (s->over_water != over) {
        Clip *d = calloc(1, sizeof *d);
        clip_init(d, W.water_layer, "splash", CK_DEBRIS, d);
        d->x = s->clip.x;
        d->y = W.water.y;
        clip_show(d);
        char lab[16];
        snprintf(lab, sizeof lab, "splash%d", W.sky);
        mc_goto_label(d->mc, lab, true);
        clip_hide(&W.water);
        clip_show(&W.water);
        snprintf(lab, sizeof lab, "water%d", W.sky);
        mc_goto_label(W.water.mc, lab, false);
        sfx("splash");
    }
    s->over_water = over;
}

// ------------------------------------------------------------ Controller
const char *world_line(int n) { return team_lines(team_type(&W.teams[1]), n); }

int world_level_score(void) {
    float s = team_average_health(&W.teams[0]) * 20 - W.teams[0].turns * 25;
    int sc = (int)floorf(s);
    if (sc < W.level * 10) sc = W.level * 10;
    return sc;
}

static void read_level(int number) {
    PakLevel L;
    pak_level(mc_pak(), number - 1, &L);
    W.lw = L.w;
    W.lh = L.h;
    W.grid = calloc((size_t)L.w * L.h, 2);
    W.bggrid = calloc((size_t)L.w * L.h, 2);
    memcpy(W.grid, L.fg, (size_t)L.w * L.h * 2);
    memcpy(W.bggrid, L.bg, (size_t)L.w * L.h * 2);
    W.tile_mc = calloc((size_t)L.w * L.h, sizeof *W.tile_mc);
    W.bgtile_mc = calloc((size_t)L.w * L.h, sizeof *W.bgtile_mc);
    const uint8_t *o = L.objs;
    for (int i = 0; i < L.nobjs; i++) {
        const char *k[40], *v[40];
        int n;
        o = pak_level_obj(mc_pak(), o, &n, k, v, 40);
        const char *type = "";
        float x = 0, y = 0;
        for (int a = 0; a < n; a++) {
            if (!strcmp(k[a], "type")) type = v[a];
            else if (!strcmp(k[a], "x")) x = (float)atof(v[a]);
            else if (!strcmp(k[a], "y")) y = (float)atof(v[a]);
        }
        if (!strcmp(type, "water")) {
            clip_init(&W.water, W.water_layer, "water", CK_PLAIN, NULL);
            clip_show(&W.water);
            W.has_water = true;
            W.water.y = W.water_y = y * 32;
            clip_update(&W.water);
        } else if (!strcmp(type, "potentialWeapons")) {
            W.npotential = 0;
            for (int a = 0; a < n; a++) {
                if (!strcmp(k[a], "x") || !strcmp(k[a], "y") || !strcmp(k[a], "type") || !strcmp(k[a], "luck") || !strcmp(k[a], "maxChests")) continue;
                WType w = weapon_from_name(k[a]);
                for (int c = 0; c < atoi(v[a]) && W.npotential < 64; c++) W.potential[W.npotential++] = w;
            }
            W.max_chests = 3;
        } else if (pak_link(mc_pak(), type) >= 0) {
            Character *c = character_new(type);
            c->s.clip.x = (x + 0.5f) * 32;
            c->s.clip.y = (y + 0.5f) * 32 + 16 - c->s.bottom;
            clip_update(&c->s.clip);
            character_set_weapons(c, k, v, n);
            if (world_give_all) {  // partida de pruebas: 5 de cada arma
                c->nhas = 0;
                for (int k5 = 0; k5 < 5; k5++)
                    for (int w = W_CHERRYBOMB; w <= W_TIDALWAVE && c->nhas < MAX_WEAPONS; w++) c->has[c->nhas++] = (WType)w;
            } else if (getenv("MUT_GIVEALL")) {  // pruebas: todas las armas
                for (int w = W_CHERRYBOMB; w <= W_TIDALWAVE && c->nhas < MAX_WEAPONS; w++) c->has[c->nhas++] = (WType)w;
            }
            int t = (!strcmp(type, "redPirate") || !strcmp(type, "redPirateCaptain")) ? 0 : 1;
            c->team = &W.teams[t];
            if (W.teams[t].n < 16) W.teams[t].chars[W.teams[t].n++] = c;
            c->index = W.teams[t].n - 1;
        }
    }
    W.ndrop = 0;
    W.drop_cols = calloc((size_t)W.lw, sizeof *W.drop_cols);
    for (int x = 0; x < W.lw; x++) {
        bool solid = false, anti = false;
        for (int y = 0; y < W.lh; y++) {
            if (W.bggrid[y * W.lw + x] == 0xFFFF) { anti = true; break; }
            if (tile_at(x, y)) { solid = true; break; }
        }
        if (solid && !anti) W.drop_cols[W.ndrop++] = x;
    }
    if (W.teams[1].ai) {
        W.speech = speech_new(world_line(0), team_speaker(&W.teams[0]));
        W.speech_line = 1;
    }
    W.anim_counter = 0;
    tiles_pan(0, 0, true);
    int l = number;
    if ((l >= 1 && l <= 5) || (l >= 16 && l <= 21)) world_set_sky(1);
    else if ((l >= 6 && l <= 10) || (l >= 22 && l <= 27)) world_set_sky(2);
    else world_set_sky(3);
    team_start_turn(W.current);
}

bool world_give_all;

void world_start(int level, int game_type) {
    memset(&W, 0, sizeof W);
    W.level = level;
    W.game_type = game_type;
    W.sky = 1;
    W.root = mc_new_root();
    W.background = mc_attach_id(W.root, 1993, "background", 0);
    mc_set_y(W.background, BG_SHIFT);
    W.holder = mc_create_empty(W.root, "holder", 1);
    W.content = mc_create_empty(W.holder, "content", 0);
    W.back_tiles = mc_create_empty(W.content, "backTiles", 0);
    W.objects = mc_create_empty(W.content, "objects", 1);
    W.effects = mc_create_empty(W.content, "effects", 2);
    W.tiles = mc_create_empty(W.content, "tiles", 3);
    W.characters = mc_create_empty(W.content, "characters", 4);
    W.water_layer = mc_create_empty(W.content, "water", 5);
    W.range_circle = mc_attach(W.content, "rangeCircle", "rangeCircle", 6);
    mc_set_visible(W.range_circle, false);
    W.text_area = mc_attach(W.root, "ingame text area", "text", 3);
    mc_set_xy(W.text_area, VIEW_CX, VIEW_H);
    W.water_y = 100000;
    team_init(&W.teams[0], 1, game_type);
    team_init(&W.teams[1], 2, game_type);
    if (getenv("MUT_AIVSAI")) W.teams[0].ai = W.teams[1].ai = true;  // pruebas: la CPU contra sí misma
    W.current = &W.teams[0];
    if (!W.teams[1].ai && frand() >= 0.5f) W.current = &W.teams[1];
    W.max_chests = 0;
    read_level(level);
    if (getenv("MUT_CHESTTEST") && W.teams[0].n && W.nchests < 8) {  // pruebas: un cofre cae sobre el primer pirata
        Character *c = W.teams[0].chars[0];
        W.chests[W.nchests++] = chest_new(c->s.clip.x, c->s.clip.y + c->s.bottom);
    }
}

void world_unload(void) {
    for (int t = 0; t < 2; t++) team_destroy(&W.teams[t]);
    for (int i = 0; i < W.nalloc; i++) free(W.alloc[i]);
    free(W.alloc);
    free(W.grid); free(W.bggrid); free(W.tile_mc); free(W.bgtile_mc); free(W.drop_cols);
    if (W.root) mc_remove(W.root);
    memset(&W, 0, sizeof W);
}

extern void app_level_won(void);

void world_next_turn(void) {
    team_finish_turn(W.current);
    bool a0 = team_any_alive(&W.teams[0]), a1 = team_any_alive(&W.teams[1]);
    if (!a0 || !a1) {
        if (W.teams[1].ai) {
            if (a0) {
                W.speech = speech_new(world_line(2), team_speaker(&W.teams[0]));
                W.speech_line = 3;
                W.score_gain = (float)world_level_score();
                app_level_won();
            } else if (a1) {
                W.speech = speech_new(world_line(3), team_speaker(&W.teams[1]));
                W.speech_line = 3;
            } else {
                W.result = 5;
                W.popup = true;
            }
            return;
        }
        W.result = a0 ? 1 : a1 ? 2 : 3;
        W.popup = true;
        extern void app_vs_result(int winner);
        app_vs_result(a0 ? 1 : a1 ? 2 : 0);
        return;
    }
    W.current = W.current->number == 1 ? &W.teams[1] : &W.teams[0];
    chest_drop_new();
    team_start_turn(W.current);
}

void world_tick(void) {
    static int logged = -1;
    if (W.result && logged != W.level * 100 + W.result) {
        logged = W.level * 100 + W.result;
        fprintf(stderr, "[game] nivel %d resultado %d turnos %d/%d\n", W.level, W.result, W.teams[0].turns, W.teams[1].turns);
    }
    W.inactivity++;
    for (int t = 0; t < 2; t++) team_advance(&W.teams[t]);
    tiles_advance();
    for (int i = 0; i < W.nmines; i++) weapon_advance(W.mines[i]);
    for (int i = 0; i < W.nchests; i++) chest_advance(W.chests[i]);
    for (int i = 0; i < W.nboxes; i++) weapon_motion(W.boxes[i]);
    water_advance();
    if (W.inactivity > 10 && !W.result) {
        W.inactivity = 0;
        if (team_turn_complete(W.current)) world_next_turn();
        else team_continue_turn(W.current);
    }
    if (W.speech) {
        speech_advance(W.speech);
        if (W.speech->complete_time > 160) {
            speech_destroy(W.speech);
            W.speech = NULL;
            if (W.speech_line == 1) {
                W.speech = speech_new(world_line(1), team_speaker(&W.teams[1]));
                W.speech_line = 2;
            } else if (W.speech_line == 3 && W.teams[1].ai) {
                if (team_any_alive(&W.teams[0])) W.result = W.level == 15 ? 6 : 4;
                else W.result = 5;
                W.popup = true;
            }
        }
    }
    text_area_tick();
}
