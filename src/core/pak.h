// Lector de data/mutiny.pak (formato descrito en tools/build_pak.py).
#ifndef MUT_PAK_H
#define MUT_PAK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { K_NONE = 0, K_SHAPE = 1, K_MORPH = 2, K_SPRITE = 3, K_SOUND = 4 };
enum { OP_PLACE = 1, OP_REMOVE = 2, OP_ACTION = 3, OP_SOUND = 4 };
enum { PF_MOVE = 1, PF_CHAR = 2, PF_MATRIX = 4, PF_CX = 8, PF_NAME = 16, PF_RATIO = 32, PF_CLIP = 64 };
enum { A_STOP = 1, A_PLAY = 2, A_GOTO_PLAY = 3, A_GOTO_STOP = 4, A_DESTROY = 5, A_CALL = 6 };

typedef struct { uint16_t page, u, v, w, h; int16_t dx, dy; uint16_t pad; } PakQuad;
typedef struct { uint8_t kind, pad; uint16_t n; uint32_t off; } PakChar;
typedef struct { float x0, y0, x1, y1; uint32_t first; uint16_t n, pad; } PakShape;
typedef struct { uint16_t ratio, pad; float x0, y0, x1, y1; uint32_t first; uint16_t n, pad2; } PakMorph;
typedef struct { uint16_t nframes, nlabels; uint32_t first_op, labels_off, frames_off; } PakSprite;
typedef struct { uint32_t name; uint16_t frame, pad; } PakLabel;
typedef struct {
    uint8_t type, flags;
    uint16_t depth, ch, ratio;
    float m[6];
    int16_t cx[8];
    uint32_t name;
    int16_t clip;
    uint16_t param;
    uint32_t arg, pad;
} PakOp;
typedef struct { uint32_t text; int16_t tracking, line_spacing; uint8_t align, font; uint16_t pad; } PakParam;
typedef struct { uint32_t rate, nsamples, data_off; } PakSound;
typedef struct { uint32_t name; uint16_t ch, pad; } PakLink;
typedef struct { uint16_t w, h; uint32_t off, csize, rawsize, format; } PakPage;  // format 0 RGBA8, 1 3DS

typedef struct {
    int w, h, players;
    const uint16_t *fg, *bg;  // w*h, fila a fila. 0 vacío, 0xFFFF "antichest", 0xFFFE sólido sin gráfico
    int nobjs;
    const uint8_t *objs;      // ver pak_level_obj
} PakLevel;

typedef struct {
    uint8_t *sect[16];
    size_t size[16];
    char id[16][5];
    int nsect;
    const char *str;
    int npages;
    const PakPage *pages;
    const uint8_t *page_base;
    uint8_t *page_cache[512];
    const PakQuad *quads;
    int nchars;
    const PakChar *chars;
    const uint8_t *cdat;
    const uint8_t *sprt;
    const PakOp *ops;
    int nparams;
    const PakParam *params;
    int nlinks;
    const PakLink *links;
    const int16_t *snd;
    const uint8_t *lvl;
    const uint8_t *tcol;
} Pak;

bool pak_load(Pak *p, const void *file, size_t size);  // copia lo que necesita: file se puede liberar
void pak_free(Pak *p);

static inline const char *pak_str(const Pak *p, uint32_t off) { return p->str + off; }
const PakChar *pak_char(const Pak *p, int id);
const PakShape *pak_shape(const Pak *p, int id);
const PakMorph *pak_morph(const Pak *p, int id, int *count);
const PakSprite *pak_sprite(const Pak *p, int id);
const PakSound *pak_sound(const Pak *p, int id);
int pak_link(const Pak *p, const char *name);  // id o -1
const uint8_t *pak_page_pixels(Pak *p, int page);  // descomprime la página la primera vez
void pak_page_release(Pak *p, int page);           // libera la copia descomprimida
uint32_t pak_tile_color(const Pak *p, int ch);  // color medio del tile (ARGB) o 0
int pak_level_count(const Pak *p);
bool pak_level(const Pak *p, int index, PakLevel *out);
// Recorre los objetos: devuelve el puntero al siguiente. attr(key,val) por cada atributo.
const uint8_t *pak_level_obj(const Pak *p, const uint8_t *obj, int *nattrs, const char **keys, const char **vals, int max);

#endif
