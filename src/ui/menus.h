// Menús del original repartidos en las dos pantallas (arriba la escena, abajo los botones).
#ifndef MUT_MENUS_H
#define MUT_MENUS_H
#include "../core/mc.h"
#include "../platform/platform.h"

enum { ST_TITLE = 1, ST_GAMESELECT, ST_LEVELS1, ST_LEVELS2, ST_HELP, ST_CREDITS, ST_SCORES, ST_CONGRATS, ST_GAME, ST_INTRO };

#define MENU_GO(st, level, type) ((st) | ((level) << 8) | ((type) << 16))

void menus_init(void);
void menus_enter(int state);
int menus_update(const Input *in);  // 0 o MENU_GO(...)
void menus_render_top(void);
void menus_render_bottom(void);
void menus_call(MC *mc, const char *stmt);

#endif
