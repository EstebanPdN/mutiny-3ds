#include "pak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

static uint8_t *find(Pak *p, const char *id, size_t *size) {
    for (int i = 0; i < p->nsect; i++)
        if (!memcmp(p->id[i], id, 4)) {
            if (size) *size = p->size[i];
            return p->sect[i];
        }
    if (size) *size = 0;
    return NULL;
}

bool pak_load(Pak *p, const void *file, size_t fsize) {
    memset(p, 0, sizeof *p);
    const uint8_t *f = file;
    if (fsize < 12 || memcmp(f, "MUTP", 4) || rd32(f + 4) != 1) {
        fprintf(stderr, "[pak] cabecera no válida\n");
        return false;
    }
    int n = (int)rd32(f + 8);
    if (n > 16) return false;
    for (int i = 0; i < n; i++) {
        const uint8_t *h = f + 12 + 20 * i;
        uint32_t off = rd32(h + 4), csize = rd32(h + 8), raw = rd32(h + 12), z = rd32(h + 16);
        if ((size_t)off + csize > fsize) return false;
        uint8_t *buf = malloc(raw ? raw : 1);
        if (!buf) return false;
        if (z) {
            uLongf out = raw;
            if (uncompress(buf, &out, f + off, csize) != Z_OK || out != raw) {
                fprintf(stderr, "[pak] sección %.4s corrupta\n", (const char *)h);
                free(buf);
                return false;
            }
        } else {
            memcpy(buf, f + off, raw);
        }
        memcpy(p->id[i], h, 4);
        p->id[i][4] = 0;
        p->sect[i] = buf;
        p->size[i] = raw;
        p->nsect++;
    }
    size_t sz;
    uint8_t *s;
    p->str = (const char *)find(p, "STR ", NULL);
    s = find(p, "PAGZ", NULL);
    p->npages = (int)rd32(s);
    p->pages = (const PakPage *)(s + 4);
    p->page_base = s;
    p->quads = (const PakQuad *)find(p, "QUAD", NULL);
    s = find(p, "CHAR", NULL);
    p->nchars = (int)rd32(s);
    p->chars = (const PakChar *)(s + 4);
    p->cdat = find(p, "CDAT", NULL);
    p->sprt = find(p, "SPRT", NULL);
    p->ops = (const PakOp *)find(p, "OPS ", NULL);
    s = find(p, "PRMS", NULL);
    p->nparams = (int)rd32(s);
    p->params = (const PakParam *)(s + 4);
    s = find(p, "LINK", NULL);
    p->nlinks = (int)rd32(s);
    p->links = (const PakLink *)(s + 4);
    p->snd = (const int16_t *)find(p, "SND ", &sz);
    p->lvl = find(p, "LVL ", NULL);
    p->tcol = find(p, "TCOL", NULL);
    return p->str && p->quads && p->cdat && p->sprt && p->ops && p->lvl;
}

void pak_free(Pak *p) {
    for (int i = 0; i < 512; i++) free(p->page_cache[i]);
    for (int i = 0; i < p->nsect; i++) free(p->sect[i]);
    memset(p, 0, sizeof *p);
}

const PakChar *pak_char(const Pak *p, int id) {
    if (id < 0 || id >= p->nchars) return NULL;
    return &p->chars[id];
}

const PakShape *pak_shape(const Pak *p, int id) {
    const PakChar *c = pak_char(p, id);
    return c && c->kind == K_SHAPE ? (const PakShape *)(p->cdat + c->off) : NULL;
}

const PakMorph *pak_morph(const Pak *p, int id, int *count) {
    const PakChar *c = pak_char(p, id);
    if (!c || c->kind != K_MORPH) return NULL;
    *count = c->n;
    return (const PakMorph *)(p->cdat + c->off);
}

const PakSprite *pak_sprite(const Pak *p, int id) {
    const PakChar *c = pak_char(p, id);
    return c && c->kind == K_SPRITE ? (const PakSprite *)(p->sprt + c->off) : NULL;
}

const PakSound *pak_sound(const Pak *p, int id) {
    const PakChar *c = pak_char(p, id);
    return c && c->kind == K_SOUND ? (const PakSound *)(p->cdat + c->off) : NULL;
}

int pak_link(const Pak *p, const char *name) {
    int lo = 0, hi = p->nlinks - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(pak_str(p, p->links[mid].name), name);
        if (!c) return p->links[mid].ch;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

const uint8_t *pak_page_pixels(Pak *p, int page) {
    if (page < 0 || page >= p->npages || page >= 512) return NULL;
    if (!p->page_cache[page]) {
        const PakPage *pg = &p->pages[page];
        uLongf out = pg->rawsize;
        uint8_t *buf = malloc(pg->rawsize);
        if (!buf || uncompress(buf, &out, p->page_base + pg->off, pg->csize) != Z_OK) {
            free(buf);
            return NULL;
        }
        p->page_cache[page] = buf;
    }
    return p->page_cache[page];
}

void pak_page_release(Pak *p, int page) {
    if (page < 0 || page >= 512) return;
    free(p->page_cache[page]);
    p->page_cache[page] = NULL;
}

uint32_t pak_tile_color(const Pak *p, int ch) {
    if (!p->tcol) return 0;
    int n = (int)rd32(p->tcol), lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t *e = p->tcol + 4 + 8 * mid;
        int c = rd16(e);
        if (c == ch) return rd32(e + 4);
        if (c < ch) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

int pak_level_count(const Pak *p) { return (int)rd32(p->lvl); }

bool pak_level(const Pak *p, int index, PakLevel *out) {
    if (index < 0 || index >= pak_level_count(p)) return false;
    const uint8_t *l = p->lvl + rd32(p->lvl + 4 + 4 * index);
    out->w = rd16(l);
    out->h = rd16(l + 2);
    out->players = l[4];
    out->nobjs = rd16(l + 6);
    out->fg = (const uint16_t *)(l + 8);
    out->bg = out->fg + out->w * out->h;
    out->objs = (const uint8_t *)(out->bg + out->w * out->h);
    return true;
}

const uint8_t *pak_level_obj(const Pak *p, const uint8_t *obj, int *nattrs, const char **keys, const char **vals, int max) {
    int n = rd16(obj);
    obj += 2;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (k < max) {
            keys[k] = pak_str(p, rd32(obj));
            vals[k] = pak_str(p, rd32(obj + 4));
            k++;
        }
        obj += 8;
    }
    *nattrs = k;
    return obj;
}
