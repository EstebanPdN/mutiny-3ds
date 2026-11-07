#include "mc.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TL_DEPTH(d) ((int)(d) - 16384)
#define DEG (3.14159265358979f / 180.0f)

enum { G_LINE = 1, G_RECT = 2 };
struct GCmd {
    uint8_t type;
    float x0, y0, x1, y1, w;
    uint32_t argb;
};

static const Pak *P;
static MCSoundHook snd_hook;
static MCCallHook call_hook;
static MC **grave;
static int ngrave, capgrave;
static int goto_nest;

static void goto_frame(MC *mc, int target, bool actions);

void mc_init(const Pak *pak, MCSoundHook s, MCCallHook c) {
    P = pak;
    snd_hook = s;
    call_hook = c;
}

const Pak *mc_pak(void) { return P; }

static const uint32_t *frame_starts(const PakSprite *s) { return (const uint32_t *)(P->cdat + s->frames_off); }

static MC *alloc_mc(void) {
    MC *m = calloc(1, sizeof *m);
    m->m = MAT_ID;
    m->cx = CX_ID;
    m->sx = m->sy = 1;
    m->visible = true;
    m->kind = MC_EMPTY;
    m->place_op = -1;
    return m;
}

static void link_child(MC *parent, MC *c) {
    c->parent = parent;
    MC **pp = &parent->child;
    while (*pp && (*pp)->depth < c->depth) pp = &(*pp)->next;
    c->next = *pp;
    *pp = c;
}

static void unlink_child(MC *c) {
    if (!c->parent) return;
    MC **pp = &c->parent->child;
    while (*pp && *pp != c) pp = &(*pp)->next;
    if (*pp) *pp = c->next;
    c->parent = NULL;
    c->next = NULL;
}

static MC *child_at(MC *mc, int depth) {
    for (MC *c = mc->child; c; c = c->next)
        if (c->depth == depth) return c;
    return NULL;
}

static void derive_props(MC *mc) {
    Mat *m = &mc->m;
    mc->sx = sqrtf(m->a * m->a + m->b * m->b);
    mc->sy = sqrtf(m->c * m->c + m->d * m->d);
    if (m->a * m->d - m->b * m->c < 0) mc->sy = -mc->sy;
    mc->rot = atan2f(m->b, m->a) / DEG;
}

static void rebuild_matrix(MC *mc) {
    float r = mc->rot * DEG, cs = cosf(r), sn = sinf(r);
    mc->m.a = mc->sx * cs;
    mc->m.b = mc->sx * sn;
    mc->m.c = -mc->sy * sn;
    mc->m.d = mc->sy * cs;
}

static void set_matrix_from_op(MC *mc, const float *m) {
    mc->m.a = m[0]; mc->m.b = m[1]; mc->m.c = m[2]; mc->m.d = m[3];
    mc->m.tx = m[4]; mc->m.ty = m[5];
    derive_props(mc);
}

static void set_cx_from_op(MC *mc, const int16_t *c) {
    mc->cx.rm = c[0] / 256.0f; mc->cx.gm = c[1] / 256.0f; mc->cx.bm = c[2] / 256.0f; mc->cx.am = c[3] / 256.0f;
    mc->cx.ra = c[4]; mc->cx.ga = c[5]; mc->cx.ba = c[6]; mc->cx.aa = c[7];
}

static void text_build(MC *mc);

static void enter_first_frame(MC *mc) {
    if (mc->kind != K_SPRITE) return;
    mc->frame = 0;
    mc->playing = mc->nframes > 1;
    goto_frame(mc, 1, true);
}

static MC *instantiate(MC *parent, int ch, int depth, const char *name) {
    MC *mc = alloc_mc();
    mc->ch = ch;
    mc->depth = depth;
    if (name) snprintf(mc->name, sizeof mc->name, "%s", name);
    const PakChar *pc = ch > 0 ? pak_char(P, ch) : NULL;
    if (pc && pc->kind != K_NONE) mc->kind = pc->kind;
    if (mc->kind == K_SPRITE) {
        mc->spr = pak_sprite(P, ch);
        mc->nframes = mc->spr ? mc->spr->nframes : 1;
    }
    link_child(parent, mc);
    return mc;
}

MC *mc_new_root(void) {
    MC *m = alloc_mc();
    snprintf(m->name, sizeof m->name, "_root");
    return m;
}

MC *mc_attach_id(MC *parent, int ch, const char *name, int depth) {
    if (!parent || ch <= 0) return NULL;
    MC *old = child_at(parent, depth);
    if (old) mc_remove(old);
    MC *mc = instantiate(parent, ch, depth, name);
    enter_first_frame(mc);
    return mc;
}

MC *mc_attach(MC *parent, const char *linkage, const char *name, int depth) {
    int id = pak_link(P, linkage);
    if (id < 0) return NULL;
    return mc_attach_id(parent, id, name ? name : linkage, depth);
}

MC *mc_create_empty(MC *parent, const char *name, int depth) {
    MC *old = child_at(parent, depth);
    if (old) mc_remove(old);
    MC *mc = alloc_mc();
    mc->depth = depth;
    if (name) snprintf(mc->name, sizeof mc->name, "%s", name);
    link_child(parent, mc);
    return mc;
}

int mc_next_depth(MC *parent) {
    int d = 0;
    for (MC *c = parent->child; c; c = c->next)
        if (c->depth >= d) d = c->depth + 1;
    return d;
}

static void kill(MC *mc) {
    mc->dead = true;
    for (MC *c = mc->child; c; c = c->next) kill(c);
}

void mc_remove(MC *mc) {
    if (!mc || mc->dead) return;
    unlink_child(mc);
    kill(mc);
    if (ngrave == capgrave) {
        capgrave = capgrave ? capgrave * 2 : 256;
        grave = realloc(grave, sizeof *grave * capgrave);
    }
    grave[ngrave++] = mc;
}

static void free_tree(MC *mc) {
    MC *c = mc->child;
    while (c) {
        MC *n = c->next;
        free_tree(c);
        c = n;
    }
    free(mc->g);
    free(mc->text);
    free(mc);
}

void mc_gc(void) {
    for (int i = 0; i < ngrave; i++) free_tree(grave[i]);
    ngrave = 0;
}

MC *mc_get(MC *mc, const char *name) {
    if (!mc) return NULL;
    for (MC *c = mc->child; c; c = c->next)
        if (!strcasecmp(c->name, name)) return c;
    return NULL;
}

MC *mc_path(MC *mc, const char *dotted) {
    char buf[128];
    snprintf(buf, sizeof buf, "%s", dotted);
    char *save = NULL;
    for (char *t = strtok_r(buf, ".", &save); t && mc; t = strtok_r(NULL, ".", &save)) mc = mc_get(mc, t);
    return mc;
}

int mc_label(MC *mc, const char *label) {
    if (!mc || !mc->spr) return 0;
    const PakLabel *l = (const PakLabel *)(P->cdat + mc->spr->labels_off);
    for (int i = 0; i < mc->spr->nlabels; i++)
        if (!strcasecmp(pak_str(P, l[i].name), label)) return l[i].frame;
    return 0;
}

// ------------------------------------------------------------ línea de tiempo
static void run_action(MC *mc, const PakOp *op) {
    switch (op->ch) {
    case A_STOP: mc->playing = false; break;
    case A_PLAY: mc->playing = true; break;
    case A_GOTO_PLAY: mc->playing = true; goto_frame(mc, (int)op->arg, true); break;
    case A_GOTO_STOP: mc->playing = false; goto_frame(mc, (int)op->arg, true); break;
    case A_DESTROY: if (call_hook) call_hook(mc, "cl.destroy()"); break;
    case A_CALL: if (call_hook) call_hook(mc, pak_str(P, op->name)); break;
    }
}

static void apply_props(MC *c, const PakOp *op) {
    if ((op->flags & PF_MATRIX) && !c->script_m) set_matrix_from_op(c, op->m);
    if ((op->flags & PF_CX) && !c->script_cx) set_cx_from_op(c, op->cx);
    if (op->flags & PF_RATIO) c->ratio = op->ratio;
    if (op->flags & PF_NAME) snprintf(c->name, sizeof c->name, "%s", pak_str(P, op->name));
    if (op->flags & PF_CLIP) { c->clip_depth = op->clip; c->is_mask = true; }
}

static int font_pirate_id = -2, font_dangle_id = -2;

static MC *place_new(MC *mc, const PakOp *op, int op_index) {
    MC *c = instantiate(mc, op->ch, TL_DEPTH(op->depth), NULL);
    c->timeline = true;
    c->place_op = op_index;
    apply_props(c, op);
    if (font_pirate_id == -2) {
        font_pirate_id = pak_link(P, "PirateFont");
        font_dangle_id = pak_link(P, "DangleFont");
    }
    bool comp = op->ch == font_pirate_id || op->ch == font_dangle_id;
    if ((op->param > 0 && op->param <= P->nparams) || comp) {
        PakParam def = {0, 1, 9, ALIGN_LEFT, FONT_PIRATE, 0};  // valores por defecto de PirateFont
        if (op->ch == font_dangle_id) { def.tracking = 0; def.line_spacing = 13; def.font = FONT_DANGLE; }
        const PakParam *pp = op->param > 0 && op->param <= P->nparams ? &P->params[op->param - 1] : &def;
        c->is_text = true;
        c->font = pp->font;
        c->align = pp->align;
        c->tracking = pp->tracking;
        c->line_spacing = pp->line_spacing;
        c->text = strdup(pp == &def ? "" : pak_str(P, pp->text));
        // UIComponent.init: _xscale = _yscale = 100
        c->sx = c->sx < 0 ? -1 : 1;
        c->sy = c->sy < 0 ? -1 : 1;
        rebuild_matrix(c);
        c->script_m = true;
        text_build(c);
    } else {
        enter_first_frame(c);
    }
    return c;
}

static void do_place(MC *mc, const PakOp *op, int op_index) {
    int d = TL_DEPTH(op->depth);
    MC *ex = child_at(mc, d);
    if (op->flags & PF_MOVE) {
        if (!ex) {
            if (op->flags & PF_CHAR) place_new(mc, op, op_index);
            return;
        }
        if ((op->flags & PF_CHAR) && ex->ch != op->ch) {
            Mat keep = ex->m;
            CX keepcx = ex->cx;
            char nm[32];
            memcpy(nm, ex->name, sizeof nm);
            mc_remove(ex);
            MC *c = instantiate(mc, op->ch, d, nm);
            c->timeline = true;
            c->place_op = op_index;
            c->m = keep;
            derive_props(c);
            c->cx = keepcx;
            apply_props(c, op);
            enter_first_frame(c);
            return;
        }
        apply_props(ex, op);
        return;
    }
    if (!(op->flags & PF_CHAR)) return;
    if (ex) mc_remove(ex);
    place_new(mc, op, op_index);
}

static void apply_frame(MC *mc, int f, bool actions) {
    const uint32_t *st = frame_starts(mc->spr);
    uint32_t a = mc->spr->first_op + st[f - 1], b = mc->spr->first_op + st[f];
    for (uint32_t i = a; i < b; i++) {
        const PakOp *op = &P->ops[i];
        if (op->type == OP_PLACE) do_place(mc, op, (int)i);
        else if (op->type == OP_REMOVE) {
            MC *ex = child_at(mc, TL_DEPTH(op->depth));
            if (ex) mc_remove(ex);
        } else if (op->type == OP_SOUND && actions && snd_hook) snd_hook(op->ch, op->flags);
    }
    if (!actions) return;
    for (uint32_t i = a; i < b && !mc->dead; i++) {
        const PakOp *op = &P->ops[i];
        if (op->type == OP_ACTION) run_action(mc, op);
    }
}

typedef struct {
    int depth;
    int create_op;
    const PakOp *create, *last;
    float m[6];
    int16_t cx[8];
    int ratio;
    uint32_t name;
    bool has_m, has_cx, has_ratio, has_name;
} Expect;

static void rewind_to(MC *mc, int target) {
    Expect ex[96];
    int n = 0;
    const uint32_t *st = frame_starts(mc->spr);
    for (int f = 1; f <= target; f++) {
        uint32_t a = mc->spr->first_op + st[f - 1], b = mc->spr->first_op + st[f];
        for (uint32_t i = a; i < b; i++) {
            const PakOp *op = &P->ops[i];
            if (op->type != OP_PLACE && op->type != OP_REMOVE) continue;
            int k;
            for (k = 0; k < n && ex[k].depth != op->depth; k++) {}
            if (op->type == OP_REMOVE) {
                if (k < n) ex[k] = ex[--n];
                continue;
            }
            bool has_char = (op->flags & PF_CHAR) != 0, move = (op->flags & PF_MOVE) != 0;
            if (has_char && (!move || k == n)) {
                if (k == n) {
                    if (n == 96) continue;
                    n++;
                }
                memset(&ex[k], 0, sizeof ex[k]);
                ex[k].depth = op->depth;
                ex[k].create = op;
                ex[k].create_op = (int)i;
            } else if (k == n) {
                continue;
            } else if (has_char && ex[k].create->ch != op->ch) {
                // Sustitución de carácter: conserva matriz y color anteriores
                ex[k].create = op;
                ex[k].create_op = (int)i;
            }
            if (op->flags & PF_MATRIX) { memcpy(ex[k].m, op->m, sizeof ex[k].m); ex[k].has_m = true; }
            if (op->flags & PF_CX) { memcpy(ex[k].cx, op->cx, sizeof ex[k].cx); ex[k].has_cx = true; }
            if (op->flags & PF_RATIO) { ex[k].ratio = op->ratio; ex[k].has_ratio = true; }
            if (op->flags & PF_NAME) { ex[k].name = op->name; ex[k].has_name = true; }
        }
    }
    bool used[96] = {0};
    MC *c = mc->child;
    while (c) {
        MC *nx = c->next;
        if (c->timeline) {
            int k;
            for (k = 0; k < n && TL_DEPTH(ex[k].depth) != c->depth; k++) {}
            if (k == n || ex[k].create_op != c->place_op) {
                mc_remove(c);
            } else {
                // Como Flash: al rebobinar, la instancia conservada vuelve a los valores de su
                // colocación (sin matriz/color = identidad). Si no, p. ej. el arma del cofre
                // se quedaba con el fundido del final de "weapon_out" al sacar la siguiente.
                static const float id_m[6] = {1, 0, 0, 1, 0, 0};
                static const int16_t id_cx[8] = {256, 256, 256, 256, 0, 0, 0, 0};
                used[k] = true;
                if (!c->script_m) set_matrix_from_op(c, ex[k].has_m ? ex[k].m : id_m);
                if (!c->script_cx) set_cx_from_op(c, ex[k].has_cx ? ex[k].cx : id_cx);
                if (ex[k].has_ratio) c->ratio = ex[k].ratio;
            }
        }
        c = nx;
    }
    for (int k = 0; k < n; k++) {
        if (used[k]) continue;
        MC *old = child_at(mc, TL_DEPTH(ex[k].depth));
        if (old) mc_remove(old);
        MC *nc = place_new(mc, ex[k].create, ex[k].create_op);
        if (ex[k].has_m && !nc->script_m) set_matrix_from_op(nc, ex[k].m);
        if (ex[k].has_cx) set_cx_from_op(nc, ex[k].cx);
        if (ex[k].has_ratio) nc->ratio = ex[k].ratio;
        if (ex[k].has_name) snprintf(nc->name, sizeof nc->name, "%s", pak_str(P, ex[k].name));
    }
}

static void goto_frame(MC *mc, int target, bool actions) {
    if (!mc->spr || mc->dead) return;
    if (target < 1) target = 1;
    if (target > mc->nframes) target = mc->nframes;
    if (target == mc->frame) return;
    if (++goto_nest > 16) {
        goto_nest--;
        return;
    }
    if (target > mc->frame) {
        for (int f = mc->frame + 1; f <= target && !mc->dead; f++) {
            mc->frame = f;
            apply_frame(mc, f, actions && f == target);
        }
    } else {
        rewind_to(mc, target);
        mc->frame = target;
        if (actions) {
            const uint32_t *st = frame_starts(mc->spr);
            uint32_t a = mc->spr->first_op + st[target - 1], b = mc->spr->first_op + st[target];
            for (uint32_t i = a; i < b; i++)
                if (P->ops[i].type == OP_SOUND && snd_hook) snd_hook(P->ops[i].ch, P->ops[i].flags);
            for (uint32_t i = a; i < b && !mc->dead; i++)
                if (P->ops[i].type == OP_ACTION) run_action(mc, &P->ops[i]);
        }
    }
    goto_nest--;
}

void mc_goto(MC *mc, int frame, bool play) {
    if (!mc) return;
    mc->playing = play;
    goto_frame(mc, frame, true);
}

bool mc_goto_label(MC *mc, const char *label, bool play) {
    int f = mc_label(mc, label);
    if (!f) return false;
    mc_goto(mc, f, play);
    return true;
}

static void tick(MC *mc) {
    MC *stack_kids[64];
    MC **kids = stack_kids;
    int n = 0, cap = 64;
    for (MC *c = mc->child; c; c = c->next) {
        if (n == cap) {
            MC **nk = malloc(sizeof *nk * cap * 2);
            memcpy(nk, kids, sizeof *nk * n);
            if (kids != stack_kids) free(kids);
            kids = nk;
            cap *= 2;
        }
        kids[n++] = c;
    }
    for (int i = 0; i < n; i++)
        if (!kids[i]->dead) tick(kids[i]);
    if (kids != stack_kids) free(kids);
    if (!mc->dead && mc->kind == K_SPRITE && mc->playing && mc->nframes > 1) {
        int nf = mc->frame + 1;
        if (nf > mc->nframes) nf = 1;
        goto_frame(mc, nf, true);
    }
}

void mc_tick(MC *root) { tick(root); }

// ------------------------------------------------------------ propiedades
void mc_set_x(MC *mc, float x) { if (mc) { mc->m.tx = x; mc->script_m = true; } }
void mc_set_y(MC *mc, float y) { if (mc) { mc->m.ty = y; mc->script_m = true; } }
void mc_set_xy(MC *mc, float x, float y) { if (mc) { mc->m.tx = x; mc->m.ty = y; mc->script_m = true; } }
void mc_set_rotation(MC *mc, float deg) {
    if (!mc) return;
    deg = fmodf(deg, 360.0f);
    if (deg > 180) deg -= 360;
    if (deg < -180) deg += 360;
    mc->rot = deg;
    rebuild_matrix(mc);
    mc->script_m = true;
}
void mc_set_xscale(MC *mc, float pct) { if (mc) { mc->sx = pct / 100.0f; rebuild_matrix(mc); mc->script_m = true; } }
void mc_set_yscale(MC *mc, float pct) { if (mc) { mc->sy = pct / 100.0f; rebuild_matrix(mc); mc->script_m = true; } }
void mc_set_alpha(MC *mc, float pct) { if (mc) { mc->cx.am = pct / 100.0f; mc->script_cx = true; } }
float mc_alpha(const MC *mc) { return mc ? mc->cx.am * 100.0f : 0; }
void mc_set_visible(MC *mc, bool v) { if (mc) mc->visible = v; }
void mc_set_cx(MC *mc, const CX *cx) { if (mc) { mc->cx = *cx; mc->script_cx = true; } }

// ------------------------------------------------------------ límites
static void grow(float b[4], bool *any, const Mat *m, float x0, float y0, float x1, float y1) {
    float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; i++) {
        float x, y;
        mat_apply(m, xs[i], ys[i], &x, &y);
        if (!*any) { b[0] = b[2] = x; b[1] = b[3] = y; *any = true; }
        if (x < b[0]) b[0] = x;
        if (y < b[1]) b[1] = y;
        if (x > b[2]) b[2] = x;
        if (y > b[3]) b[3] = y;
    }
}

static const PakMorph *morph_entry(const MC *mc) {
    int n;
    const PakMorph *e = pak_morph(P, mc->ch, &n);
    if (!e) return NULL;
    const PakMorph *best = &e[0];
    for (int i = 0; i < n; i++)
        if (abs((int)e[i].ratio - mc->ratio) < abs((int)best->ratio - mc->ratio)) best = &e[i];
    return best;
}

static void bounds_rec(MC *mc, const Mat *m, float b[4], bool *any) {
    if (mc->kind == K_SHAPE) {
        const PakShape *s = pak_shape(P, mc->ch);
        if (s && s->n) grow(b, any, m, s->x0, s->y0, s->x1, s->y1);
    } else if (mc->kind == K_MORPH) {
        const PakMorph *e = morph_entry(mc);
        if (e && e->n) grow(b, any, m, e->x0, e->y0, e->x1, e->y1);
    }
    for (int i = 0; i < mc->gn; i++) {
        GCmd *g = &mc->g[i];
        if (g->type == G_RECT) grow(b, any, m, g->x0, g->y0, g->x1, g->y1);
        else grow(b, any, m, fminf(g->x0, g->x1), fminf(g->y0, g->y1), fmaxf(g->x0, g->x1), fmaxf(g->y0, g->y1));
    }
    for (MC *c = mc->child; c; c = c->next) {
        Mat cm = mat_mul(m, &c->m);
        bounds_rec(c, &cm, b, any);
    }
}

bool mc_bounds(MC *mc, float out[4]) {
    bool any = false;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (!mc) return false;
    bounds_rec(mc, &mc->m, out, &any);
    return any;
}

float mc_width(MC *mc) { float b[4]; return mc_bounds(mc, b) ? b[2] - b[0] : 0; }
float mc_height(MC *mc) { float b[4]; return mc_bounds(mc, b) ? b[3] - b[1] : 0; }

Mat mc_world(MC *mc) {
    Mat m = MAT_ID;
    for (MC *c = mc; c; c = c->parent) m = mat_mul(&c->m, &m);
    return m;
}

void mc_global_to_local(MC *mc, float gx, float gy, float *lx, float *ly) {
    Mat w = mc_world(mc);
    float det = w.a * w.d - w.b * w.c;
    if (fabsf(det) < 1e-9f) { *lx = *ly = 0; return; }
    float x = gx - w.tx, y = gy - w.ty;
    *lx = (w.d * x - w.c * y) / det;
    *ly = (-w.b * x + w.a * y) / det;
}

bool mc_hit_point(MC *mc, float gx, float gy) {
    if (!mc) return false;
    float lx, ly, b[4];
    bool any = false;
    mc_global_to_local(mc, gx, gy, &lx, &ly);
    bounds_rec(mc, &MAT_ID, b, &any);
    return any && lx >= b[0] && lx <= b[2] && ly >= b[1] && ly <= b[3];
}

// ------------------------------------------------------------ API de dibujo
static GCmd *gpush(MC *mc) {
    if (mc->gn == mc->gcap) {
        mc->gcap = mc->gcap ? mc->gcap * 2 : 16;
        mc->g = realloc(mc->g, sizeof *mc->g * mc->gcap);
    }
    return &mc->g[mc->gn++];
}

static float line_w = 0;
static uint32_t line_c = 0;

void mc_gclear(MC *mc) { if (mc) { mc->gn = 0; mc->pen_x = mc->pen_y = 0; } }
void mc_gline_style(MC *mc, float width, uint32_t rgb, float alpha_pct) {
    (void)mc;
    if (alpha_pct < 0) alpha_pct = 0;
    if (alpha_pct > 100) alpha_pct = 100;
    line_w = width;
    line_c = ((uint32_t)(alpha_pct * 2.55f + 0.5f) << 24) | (rgb & 0xFFFFFF);
}
void mc_gmove(MC *mc, float x, float y) { mc->pen_x = x; mc->pen_y = y; }
void mc_gline(MC *mc, float x, float y) {
    GCmd *g = gpush(mc);
    g->type = G_LINE;
    g->x0 = mc->pen_x; g->y0 = mc->pen_y; g->x1 = x; g->y1 = y;
    g->w = line_w;
    g->argb = line_c;
    mc->pen_x = x;
    mc->pen_y = y;
}
void mc_grect(MC *mc, float x, float y, float w, float h, uint32_t rgb, float alpha_pct) {
    if (alpha_pct <= 0) return;
    if (alpha_pct > 100) alpha_pct = 100;
    GCmd *g = gpush(mc);
    g->type = G_RECT;
    g->x0 = x; g->y0 = y; g->x1 = x + w; g->y1 = y + h;
    g->argb = ((uint32_t)(alpha_pct * 2.55f + 0.5f) << 24) | (rgb & 0xFFFFFF);
}

// ------------------------------------------------------------ fuentes de mapa de bits
static const char *glyph_name(int font, char c, char *buf) {
    const char *pre = font == FONT_PIRATE ? "pir_" : "text_";
    const char *nm = NULL;
    switch (c) {
    case ' ': nm = "space"; break;
    case '.': nm = "stop"; break;
    case ',': nm = "comma"; break;
    case '?': nm = "question"; break;
    case '!': nm = "exclamation"; break;
    case '-': nm = "hyphen"; break;
    case '\'': if (font == FONT_DANGLE) nm = "apostrophe"; break;
    }
    if (nm) snprintf(buf, 32, "%s%s", pre, nm);
    else snprintf(buf, 32, "%s%c", pre, c);
    return buf;
}

static void text_build(MC *mc) {
    MC *base = mc_get(mc, "base");
    if (base) mc_remove(base);
    for (MC *c = mc->child; c; c = c->next) c->visible = false;  // boundingBox_mc
    base = mc_create_empty(mc, "base", 100000);
    const char *t = mc->text ? mc->text : "";
    int line = 0;
    char nm[32];
    snprintf(nm, sizeof nm, "holder%d", line);
    MC *holder = mc_create_empty(base, nm, line);
    MC *prev = NULL;
    for (int i = 0; t[i]; i++) {
        char c = (char)tolower((unsigned char)t[i]);
        if (c == '\n' || c == '\r' || c == '|') {
            if (mc->align != ALIGN_LEFT) {
                float w = mc_width(holder);
                holder->m.tx -= mc->align == ALIGN_CENTER ? roundf(w * 0.5f) : w;
            }
            line++;
            snprintf(nm, sizeof nm, "holder%d", line);
            holder = mc_create_empty(base, nm, line);
            holder->m.ty = (float)(mc->line_spacing * line);
            prev = NULL;
            continue;
        }
        char gname[32], iname[16];
        snprintf(iname, sizeof iname, "t%d", i);
        MC *g = mc_attach(holder, glyph_name(mc->font, c, gname), iname, i);
        if (!g) continue;
        if (prev) {
            MC *k = mc_get(prev, "kerning");
            float adv = (float)mc->tracking + (k ? k->m.tx : mc_width(prev));
            g->m.tx = prev->m.tx + adv;
        }
        prev = g;
    }
    if (mc->align != ALIGN_LEFT) {
        float w = mc_width(holder);
        holder->m.tx -= mc->align == ALIGN_CENTER ? roundf(w * 0.5f) : w;
    }
}

MC *mc_text_create(MC *parent, const char *name, int depth, int font, int align, int tracking, int line_spacing) {
    MC *mc = mc_create_empty(parent, name, depth);
    mc->is_text = true;
    mc->font = (uint8_t)font;
    mc->align = (uint8_t)align;
    mc->tracking = tracking;
    mc->line_spacing = line_spacing;
    mc->text = strdup("");
    return mc;
}

void mc_set_text(MC *mc, const char *text) {
    if (!mc || !mc->is_text) return;
    if (mc->text && !strcmp(mc->text, text)) return;
    free(mc->text);
    mc->text = strdup(text);
    text_build(mc);
}

const char *mc_text(const MC *mc) { return mc && mc->text ? mc->text : ""; }

// ------------------------------------------------------------ dibujo
static void draw_quads(uint32_t first, int n, const Mat *m, const CX *cx) {
    for (int i = 0; i < n; i++) {
        const PakQuad *q = &P->quads[first + i];
        gfx_image(q->page, q->u, q->v, q->w, q->h, q->dx, q->dy, m, cx);
    }
}

void mc_render(MC *mc, const Mat *base, const CX *pcx) {
    if (!mc->visible || mc->dead) return;
    Mat m = mat_mul(base, &mc->m);
    CX cx = cx_mul(pcx, &mc->cx);
    if (cx.am <= 0.002f && cx.aa <= 0) return;
    if (mc->kind == K_SHAPE) {
        const PakShape *s = pak_shape(P, mc->ch);
        if (s) draw_quads(s->first, s->n, &m, &cx);
    } else if (mc->kind == K_MORPH) {
        const PakMorph *e = morph_entry(mc);
        if (e) draw_quads(e->first, e->n, &m, &cx);
    }
    for (int i = 0; i < mc->gn; i++) {
        GCmd *g = &mc->g[i];
        uint32_t a = (uint32_t)((g->argb >> 24) * cx.am);
        if (a > 255) a = 255;
        uint32_t col = (a << 24) | (g->argb & 0xFFFFFF);
        if (g->type == G_RECT) {
            float xy[8] = {g->x0, g->y0, g->x1, g->y0, g->x1, g->y1, g->x0, g->y1};
            gfx_poly(xy, 4, col, &m);
        } else {
            gfx_line(g->x0, g->y0, g->x1, g->y1, g->w, col, &m);
        }
    }
    // Máscaras de Flash (clipDepth): la máscara no se ve y recorta a los hijos de encima
    bool masking = false;
    int mask_until = 0;
    for (MC *c = mc->child; c; c = c->next) {
        if (masking && c->depth > mask_until) {
            gfx_mask_end();
            masking = false;
        }
        if (c->is_mask) {
            if (masking) gfx_mask_end();
            gfx_mask_begin();
            bool v = c->visible;
            c->visible = true;
            mc_render(c, &m, &cx);
            c->visible = v;
            gfx_mask_use();
            masking = true;
            mask_until = c->clip_depth;
            continue;
        }
        mc_render(c, &m, &cx);
    }
    if (masking) gfx_mask_end();
}
