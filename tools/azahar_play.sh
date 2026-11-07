#!/bin/zsh
# Abre build/3ds/mutiny.3dsx en Azahar (ventana visible) con un perfil propio del proyecto
# (build/azahar-play) y el teclado como en el port de Worms. El ratón en la pantalla de abajo
# es el stylus. No toca la configuración normal de Azahar; las partidas quedan en ese perfil.
#
#   Espacio = A   Mayús = B   F = X   G = Y   Q = L   E = R   Z = ZL   C = ZR
#   Flechas = cruceta   WASD = circle pad (Alt = mitad)   Intro = START   Tab = SELECT   H = HOME
set -e
P="${0:A:h:h}"
AZ="$HOME/Applications/azahar/azahar-macos-arm64-2126.2/Azahar.app/Contents/MacOS/azahar"
IMG="${1:-$P/build/3ds/mutiny.3dsx}"
RT="$P/build/azahar-play"
CFG="$RT/xdg-config/azahar-emu"
SD="$RT/xdg-data/azahar-emu/sdmc/3ds"
mkdir -p "$CFG" "$SD/mutiny" "$RT/xdg-cache"
FW="$HOME/Library/Application Support/Azahar/sdmc/3ds/dspfirm.cdc"
[[ -f "$FW" && ! -f "$SD/dspfirm.cdc" ]] && cp "$FW" "$SD/dspfirm.cdc"
rm -f "$SD/mutiny/test.txt" "$SD/mutiny/done.txt"   # sin guion de pruebas: se juega a mano

python3 -I - "$CFG/qt-config.ini" <<'PY'
import sys
keys = {'button_a': 32, 'button_b': 16777248, 'button_x': 70, 'button_y': 71, 'button_l': 81, 'button_r': 69,
        'button_zl': 90, 'button_zr': 67, 'button_up': 16777235, 'button_down': 16777237,
        'button_left': 16777234, 'button_right': 16777236, 'button_start': 16777220,
        'button_select': 16777217, 'button_home': 72}
circle = ('down:code$083$1engine$0keyboard,engine:analog_from_button,left:code$065$1engine$0keyboard,'
          'modifier:code$016777251$1engine$0keyboard,modifier_scale:0.500000,'
          'right:code$068$1engine$0keyboard,up:code$087$1engine$0keyboard')
L = ['[System]', 'is_new_3ds=true', 'is_new_3ds\\default=false',
     '[UI]', 'firstStart=false', 'calloutFlags=4294967295', 'confirmClose=false', 'confirmClose\\default=false',
     '[Miscellaneous]', 'check_for_update_on_start=false', 'check_for_update_on_start\\default=false',
     '[Renderer]', 'graphics_api=2', 'graphics_api\\default=false',
     '[WebService]', 'enable_telemetry=false',
     '[Controls]', 'profile=0', 'profiles\\size=1', 'profiles\\1\\name=Default']
for k, code in keys.items():
    L += ['profiles\\1\\%s="code:%d,engine:keyboard"' % (k, code), 'profiles\\1\\%s\\default=false' % k]
L += ['profiles\\1\\circle_pad="%s"' % circle, 'profiles\\1\\circle_pad\\default=false',
      'profiles\\1\\touch_device=engine:emu_window', 'profiles\\1\\touch_device\\default=false',
      'profiles\\1\\use_touch_from_button=false']
open(sys.argv[1], 'w').write('\n'.join(L).replace('\\\\', '\\') + '\n')
PY

export CFFIXED_USER_HOME="$RT" XDG_DATA_HOME="$RT/xdg-data" XDG_CONFIG_HOME="$RT/xdg-config" XDG_CACHE_HOME="$RT/xdg-cache"
cd "$RT"
nohup "$AZ" "$IMG" > "$RT/azahar.out" 2>&1 &
disown
echo "Azahar abierto con $IMG (perfil $RT)"
