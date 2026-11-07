// Entrada programada para pruebas sin manos y capturas PNG, comunes a Mac y 3DS.
// Órdenes "<cuadro>:<orden>" separadas por ';':
//   k<BOTÓN> (A B X Y L R S=start E=select U D I=izq T=der)  pulsa 3 cuadros
//   t<x>,<y>   toque de 3 cuadros      h<x>,<y>,<n>  mantener n cuadros
//   d<x0>,<y0>,<x1>,<y1>,<n>  arrastre    s<nombre>  captura    q  salir
#ifndef MUT_SCRIPT_H
#define MUT_SCRIPT_H
#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

void script_load(const char *text);
// Aplica el guion al cuadro dado; devuelve false si toca salir. shot recibe el nombre de la captura.
bool script_apply(Input *in, int frame, char *shot, int shot_size);
bool script_active(void);
// PNG RGB de 8 bits (las dos pantallas apiladas como en la consola).
bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);

#endif
