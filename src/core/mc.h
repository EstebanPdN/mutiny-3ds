// Runtime mínimo de MovieClips de Flash 8: líneas de tiempo, lista de visualización,
// propiedades de AS2 (_x, _rotation, _alpha...), API de dibujo y las fuentes de mapa de bits
// PirateFont/DangleFont del juego. Reproduce la semántica que usa Mutiny, no todo Flash.
#ifndef MUT_MC_H
#define MUT_MC_H
#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "pak.h"

typedef struct MC MC;
typedef struct GCmd GCmd;

enum { MC_EMPTY = 255 };
enum { FONT_PIRATE = 0, FONT_DANGLE = 1 };
enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

struct MC {
    MC *parent, *child, *next;   // hijos ordenados por profundidad
    int depth;
    int ch;                      // carácter (0 = vacío)
    uint8_t kind;                // K_SHAPE, K_MORPH, K_SPRITE o MC_EMPTY
    bool visible, playing, timeline, script_m, script_cx, dead, is_text;
    char name[32];
    Mat m;
    CX cx;
    float rot, sx, sy;           // _rotation en grados, _xscale/_yscale en 1 = 100 %
    int frame, nframes;
    int ratio;
    const PakSprite *spr;
    int place_op;                // operación que lo colocó (para conservarlo al rebobinar)
    int clip_depth;              // máscara: recorta a los hermanos con profundidad <= clip_depth
    bool is_mask;
    // Fuente de mapa de bits
    char *text;
    uint8_t font, align;
    int tracking, line_spacing;
    // API de dibujo (moveTo/lineTo/beginFill)
    GCmd *g;
    int gn, gcap;
    float pen_x, pen_y;
    // Dueño en el juego (equivale a "mc.cl" de AS)
    void *cl;
    int user;
};

typedef void (*MCSoundHook)(int sound_id, int flags);
typedef void (*MCCallHook)(MC *mc, const char *statement);

void mc_init(const Pak *pak, MCSoundHook snd, MCCallHook call);
const Pak *mc_pak(void);

MC *mc_new_root(void);
MC *mc_attach(MC *parent, const char *linkage, const char *name, int depth);
MC *mc_attach_id(MC *parent, int ch, const char *name, int depth);
MC *mc_create_empty(MC *parent, const char *name, int depth);
int mc_next_depth(MC *parent);
void mc_remove(MC *mc);
void mc_gc(void);  // libera los clips eliminados; llamar entre cuadros

MC *mc_get(MC *mc, const char *name);
MC *mc_path(MC *mc, const char *dotted);

int mc_label(MC *mc, const char *label);  // 0 si no existe
void mc_goto(MC *mc, int frame, bool play);
bool mc_goto_label(MC *mc, const char *label, bool play);
static inline void mc_stop(MC *mc) { if (mc) mc->playing = false; }
static inline void mc_play(MC *mc) { if (mc) mc->playing = true; }
void mc_tick(MC *root);

void mc_set_x(MC *mc, float x);
void mc_set_y(MC *mc, float y);
void mc_set_xy(MC *mc, float x, float y);
void mc_set_rotation(MC *mc, float deg);
void mc_set_xscale(MC *mc, float pct);
void mc_set_yscale(MC *mc, float pct);
void mc_set_alpha(MC *mc, float pct);
float mc_alpha(const MC *mc);
void mc_set_visible(MC *mc, bool v);
void mc_set_cx(MC *mc, const CX *cx);
static inline float mc_x(const MC *mc) { return mc->m.tx; }
static inline float mc_y(const MC *mc) { return mc->m.ty; }

// Límites en el espacio del padre (como getBounds(_parent)); false si está vacío.
bool mc_bounds(MC *mc, float out[4]);
float mc_width(MC *mc);
float mc_height(MC *mc);
Mat mc_world(MC *mc);
void mc_global_to_local(MC *mc, float gx, float gy, float *lx, float *ly);
bool mc_hit_point(MC *mc, float gx, float gy);  // por caja, en coordenadas del raíz

// API de dibujo
void mc_gclear(MC *mc);
void mc_gline_style(MC *mc, float width, uint32_t rgb, float alpha_pct);
void mc_gmove(MC *mc, float x, float y);
void mc_gline(MC *mc, float x, float y);
void mc_grect(MC *mc, float x, float y, float w, float h, uint32_t rgb, float alpha_pct);

// Texto con las fuentes del juego. Crea los clips de letras como PirateFont.draw().
MC *mc_text_create(MC *parent, const char *name, int depth, int font, int align, int tracking, int line_spacing);
void mc_set_text(MC *mc, const char *text);
const char *mc_text(const MC *mc);

void mc_render(MC *mc, const Mat *base, const CX *cx);

#endif
