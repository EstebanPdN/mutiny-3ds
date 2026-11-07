# Source map

Each source file opens with a header comment describing its role. Comments inside the code are
written in Spanish; this map and the other guides are in English.

## `src/`

| Path | Purpose |
| --- | --- |
| `main.c` | Shared main loop at 25 frames per second: load the package, start audio, tick and render. |
| `core/pak.h`, `core/pak.c` | Package reader (format described in `tools/build_pak.py`). |
| `core/mc.h`, `core/mc.c` | Flash 8 MovieClip runtime: timelines, display list, AS2 properties, masks, drawing API, bitmap fonts. |
| `core/audio.h`, `core/audio.c` | Sound mixer. |
| `core/gfx.h` | Renderer interface implemented by each platform. |
| `game/game.h` | Shared game types (`World`, `Character`, `Weapon`, `Chest`…) and constants. |
| `game/world.c` | Controller, tile system, water, effects, chests, speech bubbles, level loading. |
| `game/solid.c` | Physics, characters, teams, turns and the computer player. |
| `game/weapons.c` | The 15 weapons. |
| `game/app.c`, `game/app.h` | Application states, transitions, music, saves, test match. |
| `game/lines.h` | **Generated** by `tools/gen_lines.py`; not in the repository. |
| `ui/hud.c`, `ui/hud.h` | In-game two-screen interface. |
| `ui/menus.c`, `ui/menus.h` | Intro and menus. |
| `ui/ui.c`, `ui/ui.h` | Interface helpers. |
| `platform/platform.h` | Platform interface: input, data loading, audio, frame pacing. |
| `platform/script.c`, `platform/script.h` | Scripted input and PNG screenshots for automated tests. |
| `platform/3ds/plat_3ds.c` | Nintendo 3DS platform layer. |
| `platform/3ds/gfx_c2d.c` | citro2d renderer. |
| `platform/3ds/mutiny.rsf` | CIA metadata for `makerom`. |
| `platform/mac/plat_mac.c` | macOS SDL3 platform layer. |
| `platform/soft/soft_gfx.c`, `soft_gfx.h` | Software renderer used on macOS. |

## `tools/`

| Path | Purpose |
| --- | --- |
| `extract.sh` | Extracts `Mutiny.swf` with JPEXS into `build/extract/`. |
| `build_pak.py` | Builds `mutiny.pak` (`--3ds` for the GPU texture layout). |
| `gen_ui.py` | Generates the new pixel-art interface graphics in `assets/ui/`. |
| `gen_lines.py` | Generates `src/game/lines.h`. |
| `gen_3ds_meta.py` | Generates the HOME Menu icon, banner and banner sound in `assets/3ds/`. |
| `build_3ds.sh` | Builds the 3DS `.elf`, `.3dsx` and `.cia` without make (works in paths with spaces). |
| `swfinfo.py` | Prints the timeline of any SWF symbol. A debugging aid. |
| `azahar_run.py` | Runs a scripted test in Azahar with an isolated profile and collects screenshots. |
| `azahar_play.sh` | Opens the 3DSX in Azahar with keyboard controls for manual play. |

## Root

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | macOS development build. |
| `docs/` | Guides, badges, images and legal notices. |
| `.github/` | Issue templates, code owners and funding links. |
