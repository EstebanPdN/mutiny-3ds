# Architecture

Mutiny 3DS is a native C11 reimplementation of *Mutiny* (Nitrome, 2008, Flash 8 /
ActionScript 2). It does not emulate Flash. The game logic is a direct port of the original
`com.nitrome.throwgame.*` classes. A small MovieClip runtime plays the original Flash
timelines, so animations, masks and sounds behave like they do in the SWF.

```text
Mutiny.swf ──JPEXS──▶ build/extract ──build_pak.py──▶ mutiny.pak ──▶ game (C)
                      (XML, PNG, WAV, AS)              ▲                ├─ core: pak, MovieClips, audio
                                                       │                ├─ game: logic ported from AS2
         gen_ui.py ──▶ assets/ui (new pixel-art UI) ───┘                ├─ ui:   two-screen interface
                                                                        └─ platform: 3DS / macOS
```

## 1. Data pipeline (build time)

| Step | Tool | Output |
| --- | --- | --- |
| Extract | `tools/extract.sh` with JPEXS | `build/extract/`: `swf.xml`, bitmaps, shapes at 1×, sounds, ActionScript |
| New UI art | `tools/gen_ui.py` | `assets/ui/*.png`, drawn pixel by pixel in the original palette |
| Text | `tools/gen_lines.py` | `src/game/lines.h`: dialogue and weapon descriptions from the AS source |
| Package | `tools/build_pak.py` | `mutiny.pak`: every symbol, timeline, sound, level and texture page |
| HOME Menu | `tools/gen_3ds_meta.py` | `assets/3ds/`: icon, banner and banner sound |

`mutiny.pak` is a little-endian container with zlib-compressed sections. The full format is
documented at the top of `tools/build_pak.py` and read by `src/core/pak.c`. Texture pages are
512×512. Pages are grouped by owning symbol so a level loads only the pages it uses. With
`--3ds`, pages are stored Morton-tiled in the GPU's native ABGR8 layout and compressed one page
at a time.

The repository contains none of these generated files; each developer produces them from their
own SWF.

## 2. Core runtime (`src/core`)

* **`pak.c`**: loads the package and gives typed access to characters, shapes, sprites,
  labels, sounds, linkage names and levels.
* **`mc.c`**: the MovieClip runtime. It implements the subset of Flash 8 that Mutiny uses:
  timelines with place/move/remove operations, backward `gotoAndPlay` by rebuilding the
  display list, frame labels, frame scripts compiled to small actions (`stop`, `play`,
  `gotoAndPlay`, calls back into the game), AS2 properties (`_x`, `_rotation`, `_alpha`…),
  color transforms, `clipDepth` masks, the drawing API, and the game's bitmap fonts
  (PirateFont, DangleFont).
* **`audio.c`**: mixes 16-bit mono sounds from the package into stereo for the platform layer.

The game runs at the SWF's 25 frames per second (`src/main.c`) on every platform.

## 3. Game logic (`src/game`)

Names follow the original classes so each function can be compared with the ActionScript.

| File | Original classes |
| --- | --- |
| `world.c` | `Controller`, `TileSystem`, `Clip`, `Water`, effects, `TreasureChest`, speech bubbles, level loading |
| `solid.c` | `Solid`, `Character`, `Team`: physics, turns and the computer player |
| `weapons.c` | `Weapon` and its 15 subclasses, from `CherryBomb` to `TidalWave` |
| `app.c` | Application states, music, sound effects, save data, the test match |

Port-specific changes are kept small and labelled in the code:

* Two-player chests hold 1–5 random weapon types, 1–10 of each, excluding the opening pirate's
  infinite weapons (`chest_roll_2p`).
* START + SELECT starts a test match on battle map 1 with every weapon (`world_give_all`).

## 4. Interface (`src/ui`)

* **`hud.c`**: the in-game bottom screen. The layout is fixed: team health and the menu button
  at the top, the minimap below, and a context panel that changes with the turn. Choose a
  pirate, pick a weapon, aim, place objects, action buttons, cannon and pause/result windows.
  The original mouse input is emulated in world coordinates (`W.mouse_x/y`, `W.mouse_down`).
* **`menus.c`**: the Nitrome intro, title, level select, help, scores and credits, laid out as
  two windows onto the original 550×400 stage.
* **`ui.c`**: immediate-mode helpers on top of the runtime: cached original sprites and text,
  nine-slice panels, rectangles and touch handling.

## 5. Platforms (`src/platform`)

| Layer | Nintendo 3DS | macOS (development) |
| --- | --- | --- |
| Entry | `3ds/plat_3ds.c`: libctru, input, ndsp audio, romfs/SD data | `mac/plat_mac.c`: SDL3 window with both screens, mouse = stylus |
| Graphics | `3ds/gfx_c2d.c`: citro2d, own D24S8 targets and stencil masks, lazy page upload with LRU eviction | `soft/soft_gfx.c`: software renderer with a mask buffer |
| Testing | `script.c`: scripted input and screenshots from `sdmc:/3ds/mutiny/test.txt` | `script.c`: the same scripts through `MUT_INPUT` |

Both renderers sample with nearest neighbour for pixel-perfect output. On 3DS, the package is
read from `romfs:/mutiny.pak`, or from `sdmc:/3ds/mutiny/mutiny.pak` when that file exists.
Save data lives in `sdmc:/3ds/mutiny/mutiny.sav`.
