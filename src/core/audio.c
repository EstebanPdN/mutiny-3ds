#include "audio.h"

#include <string.h>

#include "../platform/platform.h"

#define VOICES 24

typedef struct {
    const int16_t *data;
    uint32_t len;
    uint64_t pos;   // 32.32
    uint64_t step;
    int id, group;
    bool loop, on;
} Voice;

static const Pak *P;
static int rate_out = 44100;
static Voice v[VOICES];
static float gvol[2] = {1, 1};

void audio_init(const Pak *p, int out_rate) {
    P = p;
    rate_out = out_rate;
    memset(v, 0, sizeof v);
}

int audio_play(int id, bool loop, int group) {
    const PakSound *s = pak_sound(P, id);
    if (!s || !s->nsamples) return -1;
    plat_audio_lock();
    int k = -1;
    for (int i = 0; i < VOICES; i++)
        if (!v[i].on) { k = i; break; }
    if (k < 0) k = 0;
    v[k].data = P->snd + s->data_off / 2;
    v[k].len = s->nsamples;
    v[k].pos = 0;
    v[k].step = ((uint64_t)s->rate << 32) / (uint64_t)rate_out;
    v[k].id = id;
    v[k].group = group;
    v[k].loop = loop;
    v[k].on = true;
    plat_audio_unlock();
    return k;
}

void audio_stop_voice(int k) {
    if (k < 0 || k >= VOICES) return;
    plat_audio_lock();
    v[k].on = false;
    plat_audio_unlock();
}

void audio_stop_sound(int id) {
    plat_audio_lock();
    for (int i = 0; i < VOICES; i++)
        if (v[i].on && v[i].id == id) v[i].on = false;
    plat_audio_unlock();
}

void audio_stop_group(int g) {
    plat_audio_lock();
    for (int i = 0; i < VOICES; i++)
        if (v[i].on && v[i].group == g) v[i].on = false;
    plat_audio_unlock();
}

bool audio_sound_playing(int id) {
    for (int i = 0; i < VOICES; i++)
        if (v[i].on && v[i].id == id) return true;
    return false;
}

void audio_set_group_volume(int g, float vol) { if (g >= 0 && g < 2) gvol[g] = vol; }

void audio_mix(int16_t *out, int frames) {
    static int32_t acc[4096];
    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;
        memset(acc, 0, sizeof acc[0] * n);
        for (int k = 0; k < VOICES; k++) {
            Voice *x = &v[k];
            if (!x->on) continue;
            int vol = (int)(gvol[x->group] * 256);
            for (int i = 0; i < n; i++) {
                uint32_t ip = (uint32_t)(x->pos >> 32);
                if (ip >= x->len) {
                    if (!x->loop) { x->on = false; break; }
                    x->pos -= (uint64_t)x->len << 32;
                    ip = (uint32_t)(x->pos >> 32);
                }
                uint32_t fr = (uint32_t)(x->pos >> 16) & 0xFFFF;
                int32_t a = x->data[ip], b = ip + 1 < x->len ? x->data[ip + 1] : (x->loop ? x->data[0] : 0);
                acc[i] += ((a + (((b - a) * (int32_t)fr) >> 16)) * vol) >> 8;
                x->pos += x->step;
            }
        }
        for (int i = 0; i < n; i++) {
            int32_t s = acc[i];
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            out[2 * i] = out[2 * i + 1] = (int16_t)s;
        }
        out += 2 * n;
        frames -= n;
    }
}
