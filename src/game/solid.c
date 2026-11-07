// Solid, Character y Team (física, IA y turnos), portados de com.nitrome.throwgame.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define SH5(v) ((int)(v) >> 5)
#define DEGR (3.14159265358979f / 180.0f)

static float sin_t[361], cos_t[361];
static void trig_setup(void) {
    static bool done;
    if (done) return;
    for (int i = 0; i <= 360; i++) {
        sin_t[i] = (float)sin(i * 3.141592653589793 / 180);
        cos_t[i] = (float)cos(i * 3.141592653589793 / 180);
    }
    done = true;
}
float trig_sin(int d) { trig_setup(); return sin_t[((d % 360) + 360) % 360]; }
float trig_cos(int d) { trig_setup(); return cos_t[((d % 360) + 360) % 360]; }

// ------------------------------------------------------------ Solid
void solid_init(Solid *s, MC *parent, const char *link, int so) {
    clip_init(&s->clip, parent, link, CK_SOLID, s);
    s->clip.use_holder = true;
    s->so = so;
    s->left = s->right = s->top = s->bottom = 10;
    s->weight = 1;
    s->friction = 0.3f;
    s->bounce = 0.2f;
    s->draggable = true;
    s->twang_max = 20;
    s->hits_tiles = true;
    s->over_water = true;
}

void solid_update(Solid *s) {
    if (s->so == SO_WEAPON) weapon_update((Weapon *)s);
    else clip_update(&s->clip);
}

void solid_contact(Solid *s, int side) {
    if (s->so == SO_WEAPON) weapon_contact((Weapon *)s, side);
    else {
        Character *c = (Character *)s;
        if (side == FLOOR) {
            while (c->s.clip.rotation < -180) c->s.clip.rotation += 360;
            while (c->s.clip.rotation > 180) c->s.clip.rotation -= 360;
            c->s.clip.rotation *= 0.5f;
            if (c->s.clip.rotation > -1 && c->s.clip.rotation < 1) c->s.clip.rotation = 0;
        }
        if (c->contact_time > 5) sfx("hitwall");
        c->contact_time = 0;
    }
}

void solid_release(Solid *s) {
    if (s->so == SO_WEAPON) weapon_release((Weapon *)s);
    else character_release((Character *)s);
}

void solid_advance_motion(Solid *s) {
    if (s->so == SO_WEAPON) weapon_motion((Weapon *)s);
    else solid_base_motion(s);
}

void twang_prediction(Solid *s, float *vx, float *vy) {
    if (s->so == SO_WEAPON && ((Weapon *)s)->type == W_PARACHUTEBOMB) {
        if (*vy > 1) {
            *vy -= 2;
            if (*vy < 1) *vy = 1;
        }
        *vy += s->weight;
        *vx *= 0.95f;
        return;
    }
    *vy += s->weight;
}

// Velocidad del tirachinas para el ratón virtual actual
static void twang_velocity(Solid *s, float *vx, float *vy) {
    *vx = (W.mouse_x - s->clip.x) * -0.25f;
    *vy = (W.mouse_y - s->clip.y) * -0.25f;
    float d2 = *vx * *vx + *vy * *vy;
    if (d2 > s->twang_max * s->twang_max) {
        float d = sqrtf(d2);
        *vx *= s->twang_max / d;
        *vy *= s->twang_max / d;
    }
}

void solid_twang(Solid *s) {
    twang_velocity(s, &s->vx, &s->vy);
    if (s->clip.holder) mc_gclear(s->clip.holder);
    if (s->pin_shown) { clip_destroy(&s->pin); s->pin_shown = false; }
}

// DashedLine de senocular, con huecos de 8 y trazos de 8
typedef struct { float on, off, acc; bool drawing; float px, py; } Dash;

static void dash_line(MC *mc, Dash *d, float x, float y) {
    float dx = x - d->px, dy = y - d->py, L = sqrtf(dx * dx + dy * dy);
    float pos = 0;
    while (pos < L) {
        float seg = (d->drawing ? d->on : d->off) - d->acc;
        float step = fminf(seg, L - pos);
        float x0 = d->px + dx * pos / L, y0 = d->py + dy * pos / L;
        float x1 = d->px + dx * (pos + step) / L, y1 = d->py + dy * (pos + step) / L;
        if (d->drawing) { mc_gmove(mc, x0, y0); mc_gline(mc, x1, y1); }
        pos += step;
        d->acc += step;
        if (d->acc >= (d->drawing ? d->on : d->off) - 1e-4f) { d->acc = 0; d->drawing = !d->drawing; }
    }
    d->px = x;
    d->py = y;
}

void solid_draw_twang_line(Solid *s) {
    MC *h = s->clip.holder;
    if (!h) return;
    float lx = W.mouse_x - s->clip.x, ly = W.mouse_y - s->clip.y;
    float lim = s->twang_max * 4, d2 = lx * lx + ly * ly;
    if (d2 > lim * lim) {
        float d = sqrtf(d2);
        lx *= lim / d;
        ly *= lim / d;
    }
    mc_gclear(h);
    mc_gline_style(h, 2, 0xFFFFFF, 100);
    mc_gmove(h, 0, 0);
    mc_gline(h, lx, ly);
    float vx, vy;
    twang_velocity(s, &vx, &vy);
    Dash dd = {8, 8, 0, true, 0, 0};
    float px = 0, py = 0;
    for (int i = 0; i < 15; i++) {
        mc_gline_style(h, 2, 0xFFFFFF, 100 - 100 * i / 15.0f);
        twang_prediction(s, &vx, &vy);
        px += vx;
        py += vy;
        dash_line(h, &dd, px, py);
    }
    if (!s->pin_shown) {
        clip_init(&s->pin, h, "pin", CK_PLAIN, NULL);
        clip_show(&s->pin);
        s->pin_shown = true;
    }
    s->pin.x = lx;
    s->pin.y = ly;
    clip_update(&s->pin);
}

static bool box_overlap_y(Solid *s, Solid *b, float *res, bool *hit) {
    if (b->clip.x - b->left <= s->clip.x + s->right && b->clip.x + b->right >= s->clip.x - s->left) {
        if (s->vy > 0) {
            if (b->clip.y >= s->clip.y && b->clip.y - b->top <= s->clip.y + s->vy + s->bottom) {
                float r = b->clip.y - b->top - s->bottom - 0.1f;
                if (*res > r || !*hit) { *res = r; *hit = true; }
            }
        } else if (b->clip.y <= s->clip.y && b->clip.y + b->bottom >= s->clip.y + s->vy - s->top) {
            float r = b->clip.y + b->bottom + s->top + 0.1f;
            if (*res < r || !*hit) { *res = r; *hit = true; }
        }
    }
    return *hit;
}

static void box_overlap_x(Solid *s, Solid *b, float *res, bool *hit) {
    if (b->clip.y - b->top <= s->clip.y + s->bottom && b->clip.y + b->bottom >= s->clip.y - s->top) {
        if (s->vx > 0) {
            if (b->clip.x >= s->clip.x && b->clip.x - b->left <= s->clip.x + s->vx + s->right) {
                float r = b->clip.x - b->left - s->right - 0.1f;
                if (*res > r || !*hit) { *res = r; *hit = true; }
            }
        } else if (b->clip.x <= s->clip.x && b->clip.x + b->right >= s->clip.x + s->vx - s->left) {
            float r = b->clip.x + b->right + s->left + 0.1f;
            if (*res < r || !*hit) { *res = r; *hit = true; }
        }
    }
}

void solid_base_motion(Solid *s) {
    float *x = &s->clip.x, *y = &s->clip.y;
    if (W.dragging == s) {
        float dx = W.mouse_x - s->drag_x, dy = W.mouse_y - s->drag_y + 100;
        if (fabsf(dx) < 5 && fabsf(W.mouse_y - s->drag_y) < 5) {
            *x = s->drag_x;
            *y = s->drag_y;
            return;
        }
        if (dx * dx + dy * dy <= 16900) {
            float ox = *x, oy = *y;
            s->vx = (W.mouse_x - *x) / 2;
            s->vy = (W.mouse_y - *y) / 2;
            W.dragging = NULL;
            solid_advance_motion(s);
            W.dragging = s;
            s->vx = *x - ox;
            s->vy = *y - oy;
            return;
        }
        W.dragging = NULL;
        solid_release(s);
    }
    s->vy += s->weight;
    if (s->vx == 0 && s->vy == 0) return;
    float nx = *x + s->vx, ny = *y + s->vy;
    int sx = s->vx < 0 ? -1 : 1, sy = s->vy < 0 ? -1 : 1;
    int gx0 = SH5(*x - s->left * sx), gx1 = SH5(nx + s->right * sx);
    int gy0 = SH5(*y - s->top * sy), gy1 = SH5(ny + s->bottom * sy);
    bool hit = false;
    float res = 0;
    if (s->vy != 0) {
        int cx0 = SH5(*x - s->left), cx1 = SH5(*x + s->right);
        if (s->hits_tiles) {
            for (int ty = gy0 + sy; ty != gy1 + sy; ty += sy) {
                for (int tx = cx0; tx <= cx1; tx++)
                    if (tile_at(tx, ty)) hit = true;
                if (hit) {
                    res = s->vy > 0 ? (float)(ty << 5) - s->bottom - 0.1f : (float)(ty << 5) + s->top + 32.1f;
                    break;
                }
                if (abs(ty - gy0) > 4096) break;
            }
        }
        if (s->hits_boxes)
            for (int i = 0; i < W.nboxes; i++)
                if (&W.boxes[i]->s != s) box_overlap_y(s, &W.boxes[i]->s, &res, &hit);
        if (hit) {
            s->vy *= -s->bounce;
            *y = res;
            if (s->vy < 0) {
                s->vx = fmaxf(fabsf(s->vx) - s->friction, 0) * (s->vx < 0 ? -1 : 1);
                solid_contact(s, FLOOR);
            } else {
                solid_contact(s, CEILING);
            }
        } else {
            *y += s->vy;
        }
    }
    hit = false;
    if (s->vx != 0) {
        int cy0 = SH5(*y - s->top), cy1 = SH5(*y + s->bottom);
        if (s->hits_tiles) {
            for (int tx = gx0 + sx; tx != gx1 + sx; tx += sx) {
                for (int ty = cy0; ty <= cy1; ty++)
                    if (tile_at(tx, ty)) hit = true;
                if (hit) {
                    res = s->vx > 0 ? (float)(tx << 5) - s->right - 0.1f : (float)(tx << 5) + s->left + 32.1f;
                    break;
                }
                if (abs(tx - gx0) > 4096) break;
            }
        }
        if (s->hits_boxes)
            for (int i = 0; i < W.nboxes; i++)
                if (&W.boxes[i]->s != s) box_overlap_x(s, &W.boxes[i]->s, &res, &hit);
        if (hit) {
            s->vx *= -0.4f;
            *x = res;
            solid_contact(s, s->vx < 0 ? WALL_RIGHT : WALL_LEFT);
        } else {
            *x += s->vx;
        }
    }
}

// ------------------------------------------------------------ Character
Character *character_new(const char *link) {
    Character *c = world_alloc(sizeof *c);
    solid_init(&c->s, W.characters, link, SO_CHARACTER);
    c->s.left = c->s.right = 6;
    c->s.top = 8;
    c->s.bottom = 8;
    c->alive = true;
    c->health = c->max_health = c->shown_health = 100;
    c->weapon_index = -1;
    c->map_visibility = 1;
    c->ai_finished = true;
    c->can_throw = c->can_shoot = true;
    c->luck = 5;
    c->s.friction = 2;
    c->s.hits_boxes = true;
    c->s.draggable = false;
    c->s.twangable = true;
    clip_show(&c->s.clip);
    if (c->s.clip.holder) c->overlay = mc_attach(c->s.clip.holder, "characterOverlay", "overlay", mc_next_depth(c->s.clip.holder));
    return c;
}

void character_set_weapons(Character *c, const char **k, const char **v, int n) {
    c->nhas = c->ninf = 0;
    for (int i = 0; i < n; i++) {
        if (!strcmp(k[i], "x") || !strcmp(k[i], "y") || !strcmp(k[i], "type") || !strcmp(k[i], "luck") || !strcmp(k[i], "maxChests")) continue;
        WType w = weapon_from_name(k[i]);
        int cnt = atoi(v[i]);
        if (w == W_NONE) continue;
        if (cnt == 10) {
            if (c->nhas < MAX_WEAPONS) c->has[c->nhas++] = w;
            c->inf[c->ninf++] = w;
        } else {
            for (int j = 0; j < cnt && c->nhas < MAX_WEAPONS; j++) c->has[c->nhas++] = w;
        }
    }
    c->luck = 5;
    for (int i = 0; i < n; i++)
        if (!strcmp(k[i], "luck") && atoi(v[i])) c->luck = atoi(v[i]);
}

static void update_overlay(Character *c) {
    MC *o = c->overlay;
    if (!o) return;
    if (c->team->ai) mc_goto_label(o, "cpu", false);
    else mc_goto_label(o, c->team->number == 1 ? "p1" : "p2", false);
    bool current = W.current == c->team;
    bool selected = W.current->selected == c;
    bool dragged = W.dragging == &c->s || (c->weapon && W.dragging == &c->weapon->s);
    bool fired = c->thrown || (c->weapon && c->weapon->fired);
    bool moving = W.dragging == &c->s || c->thrown;
    mc_set_visible(mc_get(o, "triangle"), current && !moving && c->alive && !(W.speech && W.speech->target == c));
    MC *hb = mc_get(o, "health");
    if (hb) mc_goto(hb, 1 + (int)ceilf(27 * c->shown_health / c->max_health), false);
    mc_set_visible(hb, c->alive && !moving);
    mc_set_visible(mc_get(o, "cancelWeapon"), false);  // en 3DS se cancela desde la pantalla táctil
    (void)dragged; (void)fired;
    Weapon *sw = W.current->selected ? W.current->selected->weapon : NULL;
    if (sw && sw->type == W_VOODOODOLL) {
        mc_set_visible(mc_get(o, "target"), (sw->target == c || (!sw->target && W.hover == c)) && !sw->fired);
        mc_set_visible(mc_get(o, "corners"), false);
    } else {
        mc_set_visible(mc_get(o, "target"), false);
        mc_set_visible(mc_get(o, "corners"), ((selected && !moving) || W.hover == c) && c->alive);
    }
}

void character_advance(Character *c) {
    if (c->shown_health != c->health && c->s.vx == 0 && fabsf(c->s.vy) < 0.2f) {
        float sh = c->shown_health;
        c->shown_health = sh < c->health ? fminf(sh + 1, c->health) : fmaxf(sh - 1, c->health);
        if (c->shown_health < 1 && !c->dead_clip) {
            mc_set_visible(c->s.clip.mc, false);
            if (c->s.clip.holder) {
                c->dead_clip = mc_attach(c->s.clip.holder, "deadCharacter", "dead", mc_next_depth(c->s.clip.holder));
                mc_set_y(c->dead_clip, c->s.bottom);
            }
            c->alive = false;
            sfx("die");
        }
    }
    update_overlay(c);
    solid_base_motion(&c->s);
    if (W.dragging != &c->s) c->s.clip.rotation += c->s.vx * 3;
    clip_update(&c->s.clip);
    if (c->weapon) weapon_advance(c->weapon);
    if (!c->thrown && W.twanging == &c->s) solid_draw_twang_line(&c->s);
    if (c->s.clip.y > W.water.y) {
        c->health = 0;
        c->shown_health = 1;
        c->alive = false;
        c->s.vx *= 0.8f;
        c->s.vy *= 0.8f;
        if (c->s.vy > 1.5f) {
            c->s.vy -= 4;
            if (c->s.vy < 1.5f) c->s.vy = 1.5f;
            c->s.clip.rotation += (c->s.vx + c->s.vy) * 4;
        }
    }
    solid_splash_check(&c->s);
    if (c->hit) {
        if (c->s.vx == 0 && fabsf(c->s.vy) < 0.2f) c->hit = false;
        else mc_goto_label(c->s.clip.mc, "hit", true);
    }
    if (!c->alive && c->map_visibility > 0) c->map_visibility -= 0.1f;
    bool still = c->s.vx == 0 && fabsf(c->s.vy) <= 0.2f && c->shown_health == c->health;
    if (W.current == c->team && c->team->selected == c) {
        if (c->weapon) {
            if (!c->weapon->fired || !c->weapon->finished) still = false;
        } else if (!c->thrown) {
            still = false;
        }
    }
    if (!c->alive && (!c->weapon || !c->weapon->fired || c->weapon->finished)) still = true;
    if (c->shown_health != c->health && c->s.clip.y < W.water.y) still = false;
    if (!still) W.inactivity = 0;
    c->contact_time++;
}

int character_random_throws(Character *c, int count, AIMove *out) {
    float ox = c->s.clip.x, oy = c->s.clip.y;
    for (int i = 0; i < count; i++) {
        int ang = 180 + (int)(frand() * 180);
        float f = 5 + frand() * 15;
        float vx = trig_cos(ang) * f, vy = trig_sin(ang) * f;
        c->s.clip.x = ox;
        c->s.clip.y = oy;
        c->s.vx = vx;
        c->s.vy = vy;
        int guard = 0;
        while ((c->s.vx != 0 || fabsf(c->s.vy) > 0.2f) && c->s.clip.y < W.water.y && guard++ < 2000) solid_base_motion(&c->s);
        memset(&out[i], 0, sizeof out[i]);
        out[i].vx = vx;
        out[i].vy = vy;
        out[i].ex = c->s.clip.x;
        out[i].ey = c->s.clip.y;
    }
    c->s.clip.x = ox;
    c->s.clip.y = oy;
    c->s.vx = c->s.vy = 0;
    return count;
}

bool character_targeting_voodoo(Character *c) { return c && c->weapon && c->weapon->type == W_VOODOODOLL && !c->weapon->fired; }

void character_ai_start(Character *c) {
    c->ai_throw_checked = false;
    c->ai_weapons_checked = 0;
    c->ai_finished = false;
    c->nai = 0;
    c->has_remember = false;
    free(c->ai_throws);
    c->ai_throws = NULL;
    c->nthrows = 0;
}

static void ai_push(Character *c, AIMove *m) {
    if (c->nai == c->capai) {
        c->capai = c->capai ? c->capai * 2 : 64;
        c->ai_moves = realloc(c->ai_moves, sizeof *c->ai_moves * c->capai);
    }
    c->ai_moves[c->nai++] = *m;
}

void character_ai_think(Character *c) {
    if (!c->team->ai) return;
    if (!c->alive) { c->ai_finished = true; return; }
    Team *mine = c->team, *enemy = &W.teams[2 - c->team->number];
    int luck = c->luck;
    int ca = team_count_alive(c->team);
    luck = ca ? (int)floorf((float)luck * c->team->n / ca) : luck;
    float avx = 0, avy = 0;
    for (int i = 0; i < enemy->n; i++) { avx += enemy->chars[i]->s.clip.x; avy += enemy->chars[i]->s.clip.y; }
    avx /= enemy->n;
    avy /= enemy->n;
    if (!c->ai_throw_checked) {
        if (c->can_throw) {
            if (!c->ai_throws) {
                c->ai_throws = malloc(sizeof *c->ai_throws * 50);
                c->nthrows = character_random_throws(c, 50, c->ai_throws);
                c->nai = 0;
            }
            AIMove m = c->ai_throws[0];
            memmove(c->ai_throws, c->ai_throws + 1, sizeof *c->ai_throws * (size_t)(c->nthrows - 1));
            c->nthrows--;
            float s = 0;
            s += (m.ey - c->s.clip.y) * -0.003f;
            s += (fabsf(m.ex - avx) - fabsf(c->s.clip.x - avx)) / -500;
            s += (fabsf(m.ey - avy) - fabsf(c->s.clip.y - avy)) / -500;
            if (m.ey >= W.water.y) s -= 2;
            for (int i = 0; i < enemy->n; i++) {
                Character *e = enemy->chars[i];
                if (!e->alive) continue;
                float dx = e->s.clip.x - m.ex, dy = e->s.clip.y - m.ey, d2 = dx * dx + dy * dy, d = sqrtf(d2), k;
                if (d2 < 40000) {
                    k = 0.2f * (1 - d / 200);
                    if (d2 < 10000) {
                        k *= d / 100;
                        s -= (1 - d / 100) * 3;
                    }
                } else {
                    k = 0.3f * powf(0.75f, d / 200);
                }
                s += k;
                s *= 1 + e->evilness;
            }
            for (int i = 0; i < mine->n; i++) {
                Character *o = mine->chars[i];
                if (!o->alive) continue;
                float dx = o->s.clip.x - m.ex, dy = o->s.clip.y - m.ey, d2 = dx * dx + dy * dy;
                if (o == c) {
                    if (d2 < 6400) s -= 1 * (1 - sqrtf(d2) / 80);
                } else if (d2 < 1600) {
                    s -= 0.1f * (1 - sqrtf(d2) / 40);
                }
            }
            for (int i = 0; i < W.nchests; i++) {
                Chest *ch = W.chests[i];
                if (ch->finished) continue;
                float dx = ch->clip.x - m.ex, dy = ch->floor_y - m.ey;
                if (dx * dx + dy * dy < 1600) s += 0.5f;
            }
            // Bomba de cereza desde la posición de llegada
            int cb = character_first_weapon(c, W_CHERRYBOMB);
            character_equip(c, cb);
            if (c->weapon) {
                AIMove cbt[10];
                int nb = weapon_random_throws(c->weapon, 10, cbt);
                float best = 0;
                for (int j = 0; j < nb; j++) {
                    float k = 0;
                    for (int i = 0; i < enemy->n; i++) {
                        Character *e = enemy->chars[i];
                        if (!e->alive) continue;
                        float dx = e->s.clip.x - m.ex, dy = e->s.clip.y - m.ey, d2 = dx * dx + dy * dy;
                        if (d2 < 10000) k += (1 - sqrtf(d2) / 100) * (1 + e->evilness) * 0.5f;
                    }
                    float dx = c->s.clip.x - m.ex, dy = c->s.clip.y - m.ey, d2 = dx * dx + dy * dy;
                    if (d2 < 6400) k -= (1 - sqrtf(d2) / 80) * 1;
                    if (k > 0) {
                        s += k;
                        if (s > best) {
                            best = s;
                            // En el original aiRemember nunca recibe .weapon, así que no se reutiliza
                            c->ai_remember = cbt[j];
                            c->ai_remember.weapon = -2;
                            c->has_remember = true;
                        }
                    }
                }
            }
            character_unequip(c);
            float dx = m.ex - c->s.clip.x, dy = m.ey - c->s.clip.y, d2 = dx * dx + dy * dy;
            if (d2 < 90000) s += 0.3f * sqrtf(d2) / 300 - 0.3f;
            else s -= 0.3f;
            m.success = s;
            m.player = c;
            m.weapon = -1;
            ai_push(c, &m);
            if (c->nthrows < 1) {
                c->ai_throw_checked = true;
                if (c->nhas == 0) c->ai_finished = true;
                free(c->ai_throws);
                c->ai_throws = NULL;
            }
        } else {
            c->nai = 0;
            c->ai_throw_checked = true;
            c->ai_finished = c->nhas < 1;
            free(c->ai_throws);
            c->ai_throws = NULL;
        }
        return;
    }
    if (!c->can_shoot) {
        c->ai_weapons_checked = c->nhas;
        c->ai_finished = true;
        return;
    }
    int wi = c->ai_weapons_checked;
    for (; wi < c->nhas; wi++) {
        bool dup = false;
        for (int j = 0; j < wi; j++)
            if (c->has[j] == c->has[wi]) dup = true;
        if (!dup) break;
    }
    if (wi >= c->nhas) {
        c->ai_weapons_checked = c->nhas;
        c->ai_finished = true;
        return;
    }
    WType wt = c->has[wi];
    AIMove m;
    memset(&m, 0, sizeof m);
    m.player = c;
    m.weapon = wi;
    if (wt == W_TIDALWAVE) {
        float s = 0;
        for (int i = 0; i < enemy->n; i++) {
            Character *e = enemy->chars[i];
            if (e->alive && e->s.clip.y >= W.water.y - 300) s += e->health / e->max_health * 0.5f;
        }
        for (int i = 0; i < mine->n; i++)
            if (mine->chars[i]->alive && mine->chars[i]->s.clip.y >= W.water.y - 300) s -= 1.5f;
        m.success = s;
        ai_push(c, &m);
    } else if (wt == W_VOODOODOLL) {
        for (int i = 0; i < enemy->n; i++) {
            Character *e = enemy->chars[i];
            if (!e->alive) continue;
            AIMove r[2];
            character_random_throws(e, 2, r);
            for (int j = 0; j < 2; j++) {
                m.success = r[j].ey >= W.water.y ? 1 + frand() * 0.2f : frand() * 0.2f - 0.5f;
                m.vx = r[j].vx;
                m.vy = r[j].vy;
                m.target = e;
                ai_push(c, &m);
            }
        }
    } else if (wt == W_SEAGULL) {
        character_equip(c, wi);
        if (c->weapon) {
            int ns = 0;
            float h = 0;
            float succ = seagull_ai_simulation(c->weapon, m.shots, &ns, &h);
            if (ns > 1) {
                m.success = succ;
                m.nshots = ns;
                m.height = h;
                ai_push(c, &m);
            }
        }
        character_unequip(c);
    } else if (wt == W_WOODENCRATE || wt == W_GUNPOWDERBARREL) {
        character_equip(c, wi);
        if (c->weapon) {
            int np = box_ai_simulation(c->weapon, m.poss, 16);
            if (np >= 3) {
                m.success = frand();
                m.nposs = np;
                ai_push(c, &m);
            }
        }
        character_unequip(c);
    } else if (wt != W_CANNONBALL_ITEM) {
        character_equip(c, wi);
        if (c->weapon) {
            AIMove *r = malloc(sizeof *r * (size_t)(luck + 2));
            int nr = weapon_random_throws(c->weapon, luck, r);
            if (c->has_remember && c->ai_remember.weapon == wi) r[nr++] = c->ai_remember;
            for (int j = 0; j < nr; j++) {
                float s = -0.01f;
                for (int i = 0; i < enemy->n; i++) {
                    Character *e = enemy->chars[i];
                    if (!e->alive) continue;
                    float dx = e->s.clip.x - r[j].ex, dy = e->s.clip.y - r[j].ey, d2 = dx * dx + dy * dy;
                    if (d2 < 4900) {
                        s += 1.5f - sqrtf(d2) / 70;
                        s *= 1 + e->evilness;
                    }
                }
                for (int i = 0; i < mine->n; i++) {
                    Character *o = mine->chars[i];
                    if (!o->alive) continue;
                    float dx = o->s.clip.x - r[j].ex, dy = o->s.clip.y - r[j].ey, d2 = dx * dx + dy * dy;
                    if (d2 < 1600) s -= 1.5f - sqrtf(d2) / 40;
                }
                if (wt == W_ANCHOR) s *= 0.5f;
                if (wt == W_PIECESOFEIGHT) s = (s - 0.5f) * 1.2f;
                r[j].success = s;
                r[j].player = c;
                r[j].weapon = wi;
                ai_push(c, &r[j]);
            }
            free(r);
        }
        character_unequip(c);
    }
    c->ai_weapons_checked = wi + 1;
    if (c->ai_weapons_checked >= c->nhas) c->ai_finished = true;
}

void character_release(Character *c) {
    if (fabsf(c->s.clip.x - c->s.drag_x) < 5 && fabsf(c->s.clip.y - c->s.drag_y) < 5) {
        c->s.clip.x = c->s.drag_x;
        c->s.clip.y = c->s.drag_y;
        c->s.vx = c->s.vy = 0;
        return;
    }
    c->thrown = true;
    c->throw_finished = false;
    float v = sqrtf(c->s.vx * c->s.vx + c->s.vy * c->s.vy);
    if (v > 20) { c->s.vx = 20 * c->s.vx / v; c->s.vy = 20 * c->s.vy / v; }
    c->can_throw = false;
}

void character_twang(Character *c) {
    solid_twang(&c->s);
    c->thrown = true;
    c->can_throw = false;
}

void character_equip(Character *c, int index) {
    if ((c->weapon && c->weapon->fired) || c->thrown) return;
    if (index < 0 || index >= c->nhas) {
        if (c->weapon) character_unequip(c);
        return;
    }
    if (c->weapon_index < 0 || c->weapon_index >= c->nhas || c->has[c->weapon_index] != c->has[index] || !c->weapon) {
        if (c->weapon) character_unequip(c);
        c->weapon_index = index;
        WType t = c->has[index];
        c->weapon = t == W_CANNONBALL_ITEM ? NULL : weapon_new(t);
    }
    if (!c->weapon) return;
    c->weapon->s.clip.x = c->s.clip.x;
    c->weapon->s.clip.y = c->s.clip.y - 10;
    if (c->has[index] == W_BOULDER) c->weapon->s.clip.y -= 20;
    c->weapon->s.vx = c->weapon->s.vy = 0;
    c->weapon->owner = c;
    weapon_update(c->weapon);
}

void character_unequip(Character *c) {
    if ((c->weapon && c->weapon->fired) || c->thrown) return;
    if (c->weapon) weapon_destroy(c->weapon);
    c->weapon = NULL;
    c->weapon_index = -1;
}

void character_weapon_expired(Character *c) {
    if (c->weapon_index >= 0 && c->weapon_index < c->nhas) {
        if (!character_weapon_infinite(c, c->has[c->weapon_index])) {
            memmove(&c->has[c->weapon_index], &c->has[c->weapon_index + 1], sizeof c->has[0] * (size_t)(c->nhas - c->weapon_index - 1));
            c->nhas--;
        }
        if (c->weapon && c->weapon->limited_to_turn) weapon_destroy(c->weapon);
        c->weapon = NULL;
        c->weapon_index = -1;
    }
}

void character_subtract_health(Character *c, float amount) {
    c->health -= roundf(amount);
    if (c->health <= 0) {
        c->health = 0;
        c->alive = false;
    }
}

int character_count_weapon(Character *c, WType w) {
    int n = 0;
    for (int i = 0; i < c->nhas; i++) n += c->has[i] == w;
    return n;
}

int character_first_weapon(Character *c, WType w) {
    for (int i = 0; i < c->nhas; i++)
        if (c->has[i] == w) return i;
    return -1;
}

bool character_weapon_infinite(Character *c, WType w) {
    for (int i = 0; i < c->ninf; i++)
        if (c->inf[i] == w) return true;
    return false;
}

void character_destroy(Character *c) {
    clip_destroy(&c->s.clip);
    free(c->ai_moves);
    free(c->ai_throws);
    c->ai_moves = NULL;
    c->ai_throws = NULL;
}

// ------------------------------------------------------------ Team
void team_init(Team *t, int number, int game_type) {
    memset(t, 0, sizeof *t);
    t->number = number;
    t->ai = game_type == 1 && number == 2;
}

bool team_any_alive(Team *t) {
    for (int i = 0; i < t->n; i++)
        if (t->chars[i]->alive) return true;
    return false;
}

int team_count_alive(Team *t) {
    int n = 0;
    for (int i = 0; i < t->n; i++) n += t->chars[i]->alive;
    return n;
}

static bool is_captain(Character *c) { return strstr(c->s.clip.link, "Captain") || !strcmp(c->s.clip.link, "tribeChief"); }

Character *team_speaker(Team *t) {
    for (int i = 0; i < t->n; i++)
        if (is_captain(t->chars[i]) && t->chars[i]->alive) return t->chars[i];
    for (int i = 0; i < t->n; i++)
        if (t->chars[i]->alive) return t->chars[i];
    return t->n ? t->chars[0] : NULL;
}

Character *team_captain(Team *t) {
    for (int i = 0; i < t->n; i++)
        if (is_captain(t->chars[i])) return t->chars[i];
    return NULL;
}

const char *team_type(Team *t) {
    static char buf[2][32];
    char *b = buf[t->number - 1];
    if (!t->n) return "";
    snprintf(b, 32, "%s", t->chars[0]->s.clip.link);
    char *p;
    if ((p = strstr(b, "Captain"))) memmove(p, p + 7, strlen(p + 7) + 1);
    if ((p = strstr(b, "Chief"))) memmove(p, p + 5, strlen(p + 5) + 1);
    return b;
}

void team_select(Team *t, Character *c, bool again) {
    if (!c || !c->alive) return;
    t->selected = c;
    c->thrown = false;
    c->throw_finished = false;
    c->weapon_selected = false;
    if (!again) sfx(team_type(t));
}

void team_advance(Team *t) {
    if (t->ai) {
        if (!t->ai_finished && W.current == t) {
            for (int i = 0; i < t->n; i++) {
                if (!t->chars[i]->ai_finished) {
                    double t0 = plat_time();
                    int guard = 0;
                    do character_ai_think(t->chars[i]);
                    while (!t->chars[i]->ai_finished && plat_time() < t0 + 0.030 && guard++ < 10000);
                    break;
                }
            }
            t->ai_finished = t->chars[t->n - 1]->ai_finished;
            if (t->ai_finished) {
                AIMove *best = NULL;
                float bs = -INFINITY;
                for (int i = 0; i < t->n; i++)
                    for (int j = 0; j < t->chars[i]->nai; j++)
                        if (t->chars[i]->ai_moves[j].success > bs) {
                            bs = t->chars[i]->ai_moves[j].success;
                            best = &t->chars[i]->ai_moves[j];
                        }
                if (best && (bs > 0 || !t->ai_can_bail)) {
                    team_select(t, best->player, false);
                    t->ai_move = *best;
                    t->has_ai_move = true;
                    W.pan_to = best->player;
                } else {
                    world_next_turn();
                }
            }
        } else if (t->has_ai_move && !W.pan_to) {
            AIMove *m = &t->ai_move;
            Character *p = m->player;
            if (m->weapon >= 0) {
                character_equip(p, m->weapon);
                if (p->weapon) weapon_ai_perform(p->weapon, m);
                p->can_throw = false;
                p->can_shoot = false;
            } else {
                character_unequip(p);
                p->s.vx = m->vx;
                p->s.vy = m->vy;
                p->thrown = true;
                p->can_throw = false;
            }
            t->has_ai_move = false;
        }
    }
    for (int i = 0; i < t->n; i++) character_advance(t->chars[i]);
    if (W.current == t && !t->selected) W.inactivity = 0;
}

void team_start_turn(Team *t) {
    t->turns++;
    t->selected = NULL;
    if (t->ai) {
        t->ai_finished = false;
        for (int i = 0; i < t->n; i++) character_ai_start(t->chars[i]);
        t->ai_can_bail = false;
    }
    Character *best = NULL;
    float bd = INFINITY;
    for (int i = 0; i < t->n; i++) {
        Character *c = t->chars[i];
        if (c->nhas < 1) c->has[c->nhas++] = W_CANNONBALL_ITEM;
        c->can_shoot = c->can_throw = true;
        c->weapon_selected = c->weapon_locked = false;
        c->evilness = 0;
        if (c->alive) {
            float dx = W.cam_x + VIEW_CX - c->s.clip.x, dy = W.cam_y + VIEW_CX - c->s.clip.y, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = c; }
        }
    }
    W.pan_to = best;
    Character *cap = team_captain(t);
    if (t->turns == 1 && cap) W.pan_to = cap;
    if (t->ai) world_say("Computer, take your turn");
    else {
        char buf[64];
        snprintf(buf, sizeof buf, "Player %d, take your turn", t->number);
        world_say(buf);
    }
}

void team_finish_turn(Team *t) {
    if (t->selected) {
        t->selected->thrown = false;
        t->selected->throw_finished = false;
        character_weapon_expired(t->selected);
    }
    t->selected = NULL;
}

bool team_turn_complete(Team *t) {
    if (!t->selected || !t->selected->alive) return true;
    return !(t->selected->can_throw || t->selected->can_shoot);
}

void team_continue_turn(Team *t) {
    team_select(t, t->selected, true);
    if (t->ai) {
        t->ai_finished = false;
        for (int i = 0; i < t->n; i++) {
            character_ai_start(t->chars[i]);
            t->chars[i]->can_shoot = t->chars[i] == t->selected;
            t->chars[i]->can_throw = false;
        }
        t->ai_can_bail = true;
    }
}

float team_average_health(Team *t) {
    float s = 0;
    for (int i = 0; i < t->n; i++)
        if (t->chars[i]->alive) s += t->chars[i]->health;
    return t->n ? s / t->n : 0;
}

void team_destroy(Team *t) {
    for (int i = 0; i < t->n; i++) {
        character_unequip(t->chars[i]);
        character_destroy(t->chars[i]);
    }
}
