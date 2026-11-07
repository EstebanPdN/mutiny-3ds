#!/usr/bin/env python3
"""Prueba de la versión 3DS en Azahar con un perfil aislado y entrada programada.

Uso: tools/azahar_run.py --name prueba --seconds 60 --input "60:stitulo;100:t160,62;300:q"

El guion va a sdmc:/3ds/mutiny/test.txt (mismo formato que MUT_INPUT en Mac). Las capturas que pide
el guion las escribe el propio juego leyendo sus framebuffers; quedan en /tmp/mutiny-az/<name>/.
Usa la copia oculta de Azahar (sin icono ni foco) y anula los controles del Mac.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AZ = Path.home() / 'Applications/azahar/azahar-macos-arm64-2126.2/Azahar.app/Contents/MacOS/azahar'
AZH = Path.home() / 'Applications/azahar/hidden/Azahar.app/Contents/MacOS/azahar'
if AZH.exists() and not os.environ.get('MUT_AZ_VISIBLE'):
    AZ = AZH
NULL_KEYS = ['button_a', 'button_b', 'button_x', 'button_y', 'button_up', 'button_down', 'button_left', 'button_right',
             'button_l', 'button_r', 'button_start', 'button_select', 'button_debug', 'button_gpio14', 'button_zl',
             'button_zr', 'button_home', 'button_power', 'circle_pad', 'c_stick', 'motion_device', 'touch_device']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--name', default='prueba')
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--input', default='')
    ap.add_argument('--image', default=str(ROOT / 'build/3ds/mutiny.3dsx'))
    ap.add_argument('--old3ds', action='store_true')
    a = ap.parse_args()
    out = Path('/tmp/mutiny-az') / a.name
    shutil.rmtree(out, ignore_errors=True)
    rt = out / 'rt'
    user = rt / 'xdg-data/azahar-emu'
    cfg = rt / 'xdg-config/azahar-emu'
    sd = user / 'sdmc/3ds/mutiny'
    sd.mkdir(parents=True)
    cfg.mkdir(parents=True)
    (rt / 'xdg-cache').mkdir()
    (sd / 'test.txt').write_text(a.input.replace(';', '\n') + '\n')
    fw = Path.home() / 'Library/Application Support/Azahar/sdmc/3ds/dspfirm.cdc'
    if fw.exists():
        shutil.copy(fw, sd.parent / 'dspfirm.cdc')
    controls = ('[Controls]\nprofile=0\nprofiles\\size=1\nprofiles\\1\\name=Default\n'
                + ''.join(f'profiles\\1\\{k}=engine:null\nprofiles\\1\\{k}\\default=false\n' for k in NULL_KEYS)
                + 'profiles\\1\\use_touch_from_button=false\nprofiles\\1\\use_touchpad=false\n')
    (cfg / 'qt-config.ini').write_text(
        f'[System]\nis_new_3ds={"false" if a.old3ds else "true"}\nis_new_3ds\\default=false\n'
        '[UI]\nfirstStart=false\ncalloutFlags=4294967295\nconfirmClose=false\nconfirmClose\\default=false\n'
        '[Audio]\noutput_type=1\noutput_type\\default=false\n'
        '[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
        'log_filter=*:Info\nlog_filter\\default=false\n'
        '[Debugging]\ninstant_debug_log=true\ninstant_debug_log\\default=false\n'
        '[Renderer]\ngraphics_api=2\ngraphics_api\\default=false\nframe_limit=100\nframe_limit\\default=false\n'
        'use_vsync=false\nuse_vsync\\default=false\n'
        '[WebService]\nenable_telemetry=false\n' + controls)
    env = {**os.environ, 'CFFIXED_USER_HOME': str(rt), 'XDG_DATA_HOME': str(rt / 'xdg-data'),
           'XDG_CONFIG_HOME': str(rt / 'xdg-config'), 'XDG_CACHE_HOME': str(rt / 'xdg-cache')}
    log = open(out / 'azahar.out', 'w')
    p = subprocess.Popen([str(AZ), '-w', a.image], env=env, stdout=log, stderr=log, cwd=rt, start_new_session=True)
    t0 = time.time()
    reason = 'tiempo'
    while time.time() - t0 < a.seconds:
        time.sleep(0.5)
        if p.poll() is not None:
            reason = 'Azahar se cerró'
            break
        if (sd / 'done.txt').exists():
            reason = 'fin del guion'
            time.sleep(0.5)
            break
    try:
        os.killpg(p.pid, signal.SIGTERM)
        p.wait(5)
    except Exception:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except Exception:
            pass
    shots = sorted((sd / 'shots').glob('*.png')) if (sd / 'shots').exists() else []
    for s in shots:
        shutil.copy(s, out / s.name)
    if (sd / 'log.txt').exists():
        shutil.copy(sd / 'log.txt', out / 'log.txt')
    print(f'[azahar] {reason} tras {time.time() - t0:.1f} s, {len(shots)} capturas en {out}')
    if (out / 'log.txt').exists():
        print((out / 'log.txt').read_text()[-1500:])
    else:
        print('(sin log.txt del juego)')
        print(open(out / 'azahar.out').read()[-1500:])


if __name__ == '__main__':
    main()
