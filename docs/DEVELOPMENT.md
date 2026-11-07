# Development

This guide builds Mutiny 3DS from a clean clone. You need your own copy of the original
`Mutiny.swf`: the repository contains no Nitrome content, and every game file is extracted and
converted locally.

## Requirements

| Tool | Used for | Check |
| --- | --- | --- |
| devkitPro: devkitARM, libctru, citro2d, citro3d | 3DS build | `ls ~/devkitpro/devkitARM/bin` |
| `3dsxtool`, `smdhtool`, `bannertool`, `makerom` (in `~/devkitpro/tools/bin`) | 3DSX and CIA packaging | `~/devkitpro/tools/bin/makerom` |
| Java 11+ | JPEXS | `java -version` |
| [JPEXS Free Flash Decompiler](https://github.com/jindrapetrik/jpexs-decompiler/releases) (tested with 26.4.0) | SWF extraction | unpacked into `tools/third_party/ffdec/` |
| Python 3 with Pillow and NumPy | Data and art generation | `tools/.venv/bin/python --version` |
| FFmpeg | Banner sound | `ffmpeg -version` |
| CMake and SDL3 | macOS development build (optional) | `cmake --version` |

`tools/build_3ds.sh` reads `DEVKITPRO` and `DEVKITARM` and defaults to `~/devkitpro`. The scripts
are written for macOS (zsh) and work in paths that contain spaces.

## 1. Prepare the tools

```sh
python3 -m venv tools/.venv
tools/.venv/bin/pip install pillow numpy
# Unpack JPEXS so that tools/third_party/ffdec/ffdec.jar exists.
```

## 2. Extract and convert the game

```sh
tools/extract.sh path/to/Mutiny.swf                                        # -> build/extract/
tools/.venv/bin/python -I tools/gen_ui.py build/extract assets/ui           # new interface art
tools/.venv/bin/python -I tools/gen_lines.py build/extract src/game/lines.h # dialogue and texts
tools/.venv/bin/python -I tools/gen_3ds_meta.py                             # HOME Menu icon, banner, sound
tools/.venv/bin/python -I tools/build_pak.py build/extract assets/ui data/mutiny.pak   # macOS package
```

## 3. Build

**Nintendo 3DS**

```sh
tools/build_3ds.sh            # also builds build/3ds/romfs/mutiny.pak in the 3DS texture format
tools/build_3ds.sh --no-pak   # reuse the existing 3DS package after code-only changes
```

Output: `build/3ds/mutiny.3dsx` and `build/3ds/mutiny.cia`, with the package embedded in romfs.

**macOS** (development build: both screens in one window, the mouse acts as the stylus)

```sh
cmake -S . -B build/mac && cmake --build build/mac
build/mac/mutiny
```

Keyboard: arrows = D-Pad, WASD = Circle Pad, Space = A, Left Shift = B, F = X, G = Y,
Q / E = L / R, Return = START, Tab = SELECT. Game controllers are supported through SDL.

## 4. Test

The macOS build accepts environment variables for hands-free tests:

| Variable | Effect |
| --- | --- |
| `MUT_HIDDEN=1` | No window and no frame pacing. |
| `MUT_INPUT="<frame>:<command>;…"` | Scripted input. Commands: `kA`, `kB`, `kS` (START), `kE` (SELECT)… press a button; `tX,Y` tap; `hX,Y,N` hold; `dX0,Y0,X1,Y1,N` drag; `sNAME` screenshot to `build/mac/shots/NAME.png`; `q` quit. |
| `MUT_LEVEL=n`, `MUT_TYPE=1/2`, `MUT_SEED=n` | Start directly in a level, game type and random seed. |
| `MUT_MENU=n` | Open a menu state directly. |
| `MUT_AIVSAI=1` | The computer plays both teams. |
| `MUT_GIVEALL=1` | Every pirate also gets every weapon. |
| `MUT_FORCE_RESULT=n` | Show an end-of-level window on frame 40. |
| `MUT_CHESTTEST=1` | Drop a chest on player 1's first pirate. |
| `MUT_MUTE=1` | No audio. |

Example: start the test match from the title screen and capture the weapon grid.

```sh
MUT_HIDDEN=1 MUT_INPUT="300:kS;300:kE;400:t60,165;420:sweapons;421:q" build/mac/mutiny
```

On 3DS, the same script format is read from `sdmc:/3ds/mutiny/test.txt`. `tools/azahar_run.py`
runs it in Azahar with an isolated profile and collects screenshots taken from the real
framebuffers. `tools/azahar_play.sh` opens the build in Azahar for manual play.

Emulator and macOS results are not hardware validation. Test on a real console before release.
