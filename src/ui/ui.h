// Utilidades de interfaz en modo inmediato sobre el runtime de MovieClips:
// sprites y textos originales cacheados por clave, rectángulos, paneles de 9 trozos
// y toques en la pantalla táctil.
#ifndef MUT_UI_H
#define MUT_UI_H
#include <stdbool.h>

#include "../core/mc.h"
#include "../platform/platform.h"

// Paleta de los paneles originales de Mutiny
#define COL_PANEL 0xFF303030u
#define COL_OUTLINE 0xFF2A2222u
#define COL_RED 0xFFE63013u
#define COL_RED_DK 0xFFB5190Fu
#define COL_RED_LT 0xFFF28674u
#define COL_BLUE 0xFF3634D0u
#define COL_BLUE_DK 0xFF1E1CA3u
#define COL_BLUE_LT 0xFF8887E5u
#define COL_WHITE 0xFFF8F8F8u

void ui_init(void);
void ui_tick(void);
void ui_input(const Input *in);

MC *ui_mc(const char *key, const char *link);
MC *ui_mc_id(const char *key, int ch);
MC *ui_textmc(const char *key, int font, int align, int tracking, int spacing);
void ui_draw(MC *mc, float x, float y);
void ui_draw_cx(MC *mc, float x, float y, const CX *cx);
void ui_draw_m(MC *mc, const Mat *m, const CX *cx);
void ui_sprite(const char *key, const char *link, const char *label, float x, float y);
void ui_text(const char *key, int font, int align, float x, float y, const char *text);
void ui_text_cx(const char *key, int font, int align, float x, float y, const char *text, const CX *cx);
float ui_text_width(const char *key, int font, const char *text);

void ui_rect(float x, float y, float w, float h, uint32_t argb);
void ui_line(float x0, float y0, float x1, float y1, float w, uint32_t argb);
void ui_frame(float x, float y, float w, float h, uint32_t argb);  // borde de 1 px
// Panel de 9 trozos a partir de la primera imagen de una forma (insets en píxeles).
void ui_nine(int shape_ch, int inset, float x, float y, float w, float h, const CX *cx);
// Imagen generada (assets/ui/<nombre>.png -> linkage ui_<nombre>)
void ui_image(const char *name, float x, float y);
void ui_image_cx(const char *name, float x, float y, const CX *cx);

// Toques
bool ui_touching(void);
int ui_tx(void);
int ui_ty(void);
bool ui_hover(float x, float y, float w, float h);
bool ui_touch_began(float x, float y, float w, float h);
bool ui_tap(float x, float y, float w, float h);
bool ui_touch_began_any(void);
bool ui_touch_ended(void);
int ui_start_x(void);
int ui_start_y(void);
const Input *ui_in(void);

#endif
