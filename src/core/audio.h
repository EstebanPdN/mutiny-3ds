// Mezclador de los sonidos del juego (PCM 16 bits mono del pak) a estéreo.
#ifndef MUT_AUDIO_H
#define MUT_AUDIO_H
#include <stdbool.h>
#include <stdint.h>

#include "pak.h"

enum { AUDIO_MUSIC = 0, AUDIO_SFX = 1 };

void audio_init(const Pak *p, int out_rate);
int audio_play(int sound_id, bool loop, int group);  // devuelve la voz o -1
void audio_stop_voice(int voice);
void audio_stop_sound(int sound_id);
void audio_stop_group(int group);
bool audio_sound_playing(int sound_id);
void audio_set_group_volume(int group, float v);
void audio_mix(int16_t *stereo, int frames);  // hilo de audio

#endif
