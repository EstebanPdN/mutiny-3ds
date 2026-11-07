// Bucle principal común: 25 cuadros por segundo como el SWF original.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/audio.h"
#include "core/gfx.h"
#include "core/mc.h"
#include "core/pak.h"
#include "game/app.h"
#include "platform/platform.h"

static Pak pak;

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!plat_init("Mutiny - 3DS (Mac)")) return 1;
    size_t size = 0;
    fprintf(stderr, "[main] cargando mutiny.pak\n");
    void *file = plat_load_data("mutiny.pak", &size);
    fprintf(stderr, "[main] pak %u bytes\n", (unsigned)size);
    if (!file || !pak_load(&pak, file, size)) {
        fprintf(stderr, "No se pudo cargar data/mutiny.pak (ejecuta tools/build_pak.py)\n");
        return 1;
    }
    free(file);
    fprintf(stderr, "[main] pak listo: %d páginas, %d caracteres\n", pak.npages, pak.nchars);
    gfx_upload_pages(&pak);
    audio_init(&pak, plat_audio_rate());
    plat_audio_start(plat_audio_rate(), audio_mix);
    app_init(&pak);
    fprintf(stderr, "[main] juego iniciado\n");
    const double dt = 1.0 / 25.0;
    double next = plat_time();
    Input in;
    while (plat_poll(&in)) {
        app_tick(&in);
        app_render();
        plat_present();
        static double t250;
        if (plat_frame() == 1 || plat_frame() % 250 == 0) {
            double now = plat_time();
            fprintf(stderr, "[main] cuadro %d (%.1f cuadros/s)\n", plat_frame(), t250 > 0 ? 250 / (now - t250) : 0.0);
            t250 = now;
        }
        mc_gc();
        next += dt;
        double now = plat_time();
        if (next > now) plat_sleep(next - now);
        else if (now - next > 0.5) next = now;
    }
    app_shutdown();
    plat_shutdown();
    pak_free(&pak);
    return 0;
}
