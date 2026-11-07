// Interfaz entre el juego y cada plataforma (macOS con SDL3, 3DS con libctru).
#ifndef MUT_PLATFORM_H
#define MUT_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    BTN_A = 1 << 0, BTN_B = 1 << 1, BTN_X = 1 << 2, BTN_Y = 1 << 3,
    BTN_START = 1 << 4, BTN_SELECT = 1 << 5, BTN_L = 1 << 6, BTN_R = 1 << 7,
    BTN_UP = 1 << 8, BTN_DOWN = 1 << 9, BTN_LEFT = 1 << 10, BTN_RIGHT = 1 << 11,
    BTN_ZL = 1 << 12, BTN_ZR = 1 << 13, BTN_TOUCH = 1 << 14,
};

typedef struct {
    uint32_t held, down, up;  // botones (down/up: solo en este cuadro)
    bool touch;               // stylus en la pantalla de abajo
    int tx, ty;               // coordenadas táctiles (0..319, 0..239)
    float cx, cy;             // circle pad, -1..1 (y positivo hacia abajo)
} Input;

bool plat_init(const char *title);
void plat_shutdown(void);
bool plat_poll(Input *in);   // false: cerrar
void plat_present(void);     // muestra las dos pantallas dibujadas con gfx_*
double plat_time(void);
void plat_sleep(double s);
int plat_frame(void);        // cuadros presentados

void *plat_load_data(const char *name, size_t *size);  // datos de solo lectura (data/ o romfs)
bool plat_save_write(const char *name, const void *data, size_t size);
void *plat_save_read(const char *name, size_t *size);

typedef void (*AudioPull)(int16_t *stereo, int frames);
bool plat_audio_start(int rate, AudioPull pull);
int plat_audio_rate(void);  // frecuencia de salida de la plataforma
void plat_audio_lock(void);
void plat_audio_unlock(void);

// Captura de las dos pantallas en PNG (pruebas). Sin efecto si la plataforma no lo admite.
void plat_screenshot(const char *path);

#endif
