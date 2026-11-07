#!/bin/zsh
# Compila la versión de 3DS sin make (la ruta del proyecto tiene espacios):
#   pak con texturas en formato de la GPU -> build/3ds/romfs/mutiny.pak
#   build/3ds/mutiny.elf / .smdh / .3dsx (con el romfs dentro) y mutiny.cia
# Uso: tools/build_3ds.sh [--no-pak]
set -e
P="${0:A:h:h}"
export DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
B="$DEVKITARM/bin/arm-none-eabi"
OUT="$P/build/3ds"
OBJ="$OUT/obj"
mkdir -p "$OBJ" "$OUT/romfs"
cd "$P"
if [[ $1 != --no-pak ]]; then
  tools/.venv/bin/python -I tools/build_pak.py build/extract assets/ui "$OUT/romfs/mutiny.pak" --3ds
fi
ARCH=(-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft)
CFLAGS=($ARCH -O2 -g -std=gnu11 -Wall -Wno-unused-parameter -Wno-missing-field-initializers
        -mword-relocations -ffunction-sections -D__3DS__
        -I src -I "$DEVKITPRO/libctru/include" -I "$DEVKITPRO/portlibs/3ds/include")
SRCS=(src/main.c src/core/*.c src/game/*.c src/ui/*.c src/platform/script.c src/platform/3ds/*.c)
OBJS=()
for f in $SRCS; do
  o="$OBJ/${${f//\//_}%.c}.o"
  OBJS+=("$o")
  if [[ ! -f "$o" || "$f" -nt "$o" || src/core/gfx.h -nt "$o" || src/game/game.h -nt "$o" || src/core/mc.h -nt "$o" || src/core/pak.h -nt "$o" ]]; then
    "$B-gcc" $CFLAGS -c "$f" -o "$o" &
  fi
done
wait
for o in $OBJS; do [[ -f "$o" ]] || { echo "falta $o"; exit 1; }; done
"$B-gcc" -specs=3dsx.specs $ARCH -g $OBJS -L "$DEVKITPRO/libctru/lib" -L "$DEVKITPRO/portlibs/3ds/lib" \
  -lcitro2d -lcitro3d -lctru -lz -lm -Wl,-Map,"$OUT/mutiny.map" -o "$OUT/mutiny.elf"
"$DEVKITPRO/tools/bin/smdhtool" --create "Mutiny" "Nitrome pirate artillery - two screens" "Nitrome 2008 / 3DS port" \
  assets/3ds/icon.png "$OUT/mutiny.smdh"
"$DEVKITPRO/tools/bin/3dsxtool" "$OUT/mutiny.elf" "$OUT/mutiny.3dsx" --smdh="$OUT/mutiny.smdh" --romfs="$OUT/romfs"
# CIA para instalar en el menú HOME (banner, icono y sonido de tools/gen_3ds_meta.py)
T="$DEVKITPRO/tools/bin"
"$T/bannertool" makebanner -i assets/3ds/banner.png -a assets/3ds/banner.wav -o "$OUT/banner.bnr" >/dev/null
"$T/bannertool" makesmdh -s "Mutiny" -l "Nitrome pirate artillery" -p "Nitrome / 3DS port" \
  -i assets/3ds/icon.png -o "$OUT/icon.icn" >/dev/null
(cd "$OUT" && "$T/makerom" -f cia -o mutiny.cia -elf mutiny.elf -rsf "$P/src/platform/3ds/mutiny.rsf" -icon icon.icn -banner banner.bnr -target t)
ls -la "$OUT/mutiny.3dsx" "$OUT/mutiny.cia"
shasum -a 256 "$OUT/mutiny.3dsx" "$OUT/mutiny.cia"
