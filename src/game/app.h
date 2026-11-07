#ifndef MUT_APP_H
#define MUT_APP_H
#include "../core/pak.h"
#include "../platform/platform.h"

void app_init(const Pak *pak);
void app_tick(const Input *in);
void app_render(void);
void app_shutdown(void);

#endif
