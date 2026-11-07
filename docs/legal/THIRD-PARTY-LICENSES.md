# Third-party licenses and notices

## Game content

*Mutiny* © 2008 Nitrome Ltd. The artwork, animation, music, sound effects, text and level
design belong to Nitrome. They are **not** covered by this repository's license.

* The repository contains no Nitrome files. Developers extract them locally from their own copy
  of `Mutiny.swf`.
* The release builds (`.cia`, `.3dsx`) contain the game data converted from the original SWF.
  This includes the HOME Menu icon, banner and banner sound, which are made from the original
  logo, the red captain sprite and the Nitrome intro sounds.
* This is an unofficial fan port, not affiliated with or endorsed by Nitrome. Rights holders can
  open an issue to request removal.

Original game credits: programming by Chris Burt-Brown and Heather Stancliffe, artwork by
Jon Annal and Mat Annal, music by Dave Cowen.

## Port source code

The original port source code (`src/`, `tools/`, build files and documentation) is licensed
under the GNU General Public License v3.0 or later. See [LICENSE](../../LICENSE).

## Libraries linked into the builds

| Component | Use | License |
| --- | --- | --- |
| [libctru](https://github.com/devkitPro/libctru) | 3DS system library | zlib |
| [citro3d](https://github.com/devkitPro/citro3d) | 3DS GPU library | zlib |
| [citro2d](https://github.com/devkitPro/citro2d) | 3DS 2D rendering | zlib |
| [zlib](https://zlib.net/) | Package decompression | zlib |
| [SDL3](https://www.libsdl.org/) | macOS development build only | zlib |
| newlib (devkitARM) | C library on 3DS | newlib licenses, see devkitPro |

## Build-time tools (not distributed)

| Tool | Use | License |
| --- | --- | --- |
| [JPEXS Free Flash Decompiler](https://github.com/jindrapetrik/jpexs-decompiler) | SWF extraction | GPL-3.0 |
| [bannertool](https://github.com/Epicpkmn11/bannertool) | HOME Menu banner and icon | MIT |
| [makerom (Project_CTR)](https://github.com/3DSGuy/Project_CTR) | CIA packaging | MIT |
| 3dsxtool, smdhtool ([3dstools](https://github.com/devkitPro/3dstools)) | 3DSX packaging | No license file published upstream |
| [Pillow](https://python-pillow.org/), [NumPy](https://numpy.org/) | Image generation | MIT-CMU, BSD-3-Clause |
| [FFmpeg](https://ffmpeg.org/) | Banner sound mixing | LGPL-2.1+ / GPL |
| [Azahar](https://github.com/azahar-emu/azahar) | Emulator testing | GPL-3.0 |

## Repository presentation

Badges, download buttons and the Website, Discord and Patreon buttons come from the author's
own repositories. The README images are real screenshots of this port and show Nitrome's
artwork.
