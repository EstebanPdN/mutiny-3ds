#include "script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct { int frame; char cmd; int a[5]; char name[64]; } Cmd;
static Cmd cmds[512];
static int ncmds;

void script_load(const char *text) {
    ncmds = 0;
    if (!text) return;
    char *buf = strdup(text), *save = NULL;
    for (char *t = strtok_r(buf, ";\n", &save); t && ncmds < 512; t = strtok_r(NULL, ";\n", &save)) {
        while (*t == ' ' || *t == '\r') t++;
        char *colon = strchr(t, ':');
        if (!colon) continue;
        Cmd *c = &cmds[ncmds];
        memset(c, 0, sizeof *c);
        c->frame = atoi(t);
        c->cmd = colon[1];
        if (c->cmd == 'k' || c->cmd == 's') {
            snprintf(c->name, sizeof c->name, "%s", colon + 2);
            char *e = c->name + strlen(c->name);
            while (e > c->name && (e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        } else {
            sscanf(colon + 2, "%d,%d,%d,%d,%d", &c->a[0], &c->a[1], &c->a[2], &c->a[3], &c->a[4]);
        }
        ncmds++;
    }
    free(buf);
}

bool script_active(void) { return ncmds > 0; }

static uint32_t key_bit(const char *k) {
    switch (toupper((unsigned char)k[0])) {
    case 'A': return BTN_A; case 'B': return BTN_B; case 'X': return BTN_X; case 'Y': return BTN_Y;
    case 'L': return BTN_L; case 'R': return BTN_R; case 'S': return BTN_START; case 'E': return BTN_SELECT;
    case 'U': return BTN_UP; case 'D': return BTN_DOWN; case 'I': return BTN_LEFT; case 'T': return BTN_RIGHT;
    }
    return 0;
}

bool script_apply(Input *in, int frame, char *shot, int shot_size) {
    bool run = true;
    if (shot) shot[0] = 0;
    for (int i = 0; i < ncmds; i++) {
        Cmd *c = &cmds[i];
        int f = frame - c->frame;
        switch (c->cmd) {
        case 'k': if (f >= 0 && f < 3) in->held |= key_bit(c->name); break;
        case 't': if (f >= 0 && f < 3) { in->touch = true; in->tx = c->a[0]; in->ty = c->a[1]; } break;
        case 'h': if (f >= 0 && f < c->a[2]) { in->touch = true; in->tx = c->a[0]; in->ty = c->a[1]; } break;
        case 'd': {
            int n = c->a[4] > 0 ? c->a[4] : 10;
            if (f >= 0 && f <= n) {
                in->touch = true;
                in->tx = c->a[0] + (c->a[2] - c->a[0]) * f / n;
                in->ty = c->a[1] + (c->a[3] - c->a[1]) * f / n;
            }
            break;
        }
        case 's': if (f == 0 && shot) snprintf(shot, (size_t)shot_size, "%s", c->name); break;
        case 'q': if (f >= 0) run = false; break;
        }
    }
    return run;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t h[8] = {(uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len,
                    (uint8_t)type[0], (uint8_t)type[1], (uint8_t)type[2], (uint8_t)type[3]};
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, h + 4, 4);
    if (len) crc = crc32(crc, data, len);
    uint8_t c[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    fwrite(c, 1, 4, f);
}

bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h) {
    size_t rowb = 1 + (size_t)w * 3;
    uint8_t *raw = malloc(rowb * h);
    if (!raw) return false;
    for (int y = 0; y < h; y++) {
        raw[y * rowb] = 0;
        memcpy(raw + y * rowb + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    uLongf zl = compressBound((uLong)(rowb * h));
    uint8_t *z = malloc(zl);
    bool ok = z && compress2(z, &zl, raw, (uLong)(rowb * h), 6) == Z_OK;
    FILE *f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        fwrite(sig, 1, 8, f);
        uint8_t ihdr[13] = {0, 0, (uint8_t)(w >> 8), (uint8_t)w, 0, 0, (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
        chunk(f, "IHDR", ihdr, 13);
        chunk(f, "IDAT", z, (uint32_t)zl);
        chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(z);
    free(raw);
    return f != NULL;
}
