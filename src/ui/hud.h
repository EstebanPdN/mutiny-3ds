// Interfaz de la partida en dos pantallas: arriba el juego, abajo los controles táctiles.
#ifndef MUT_HUD_H
#define MUT_HUD_H
#include <stdbool.h>

#include "../platform/platform.h"

enum { POP_NONE = 0, POP_NEXT, POP_RESTART, POP_QUIT, POP_CONTINUE, POP_CONGRATS };

void hud_reset(void);
void hud_update(const Input *in);
void hud_render_top(void);
void hud_render_bottom(void);
int hud_popup_action(void);  // acción elegida en la ventana de fin de nivel/pausa
void hud_scroll_input(int *dx, int *dy);
void hud_fan(bool on);
void hud_pause(void);

#endif
