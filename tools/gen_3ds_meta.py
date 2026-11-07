#!/usr/bin/env python3
"""Genera, píxel a píxel, lo que la 3DS muestra en el menú HOME:
  assets/3ds/icon.png    48x48  cara del capitán rojo (sprite 853 a 2x) sobre rayos rojos
  assets/3ds/banner.png  256x128 logo MUTINY lo más grande posible, fondo transparente
  assets/3ds/banner.wav  sonido de la intro de Nitrome (497 x3 + 485, como en el juego), mono 16 bits
Uso: tools/.venv/bin/python -I tools/gen_3ds_meta.py [build/extract]"""
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_ui import drop_even, load  # noqa: E402

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(P, 'build', 'extract')
OUT = os.path.join(P, 'assets', '3ds')

CAPTAIN, LOGO = 853, 1921
# Sonidos de la intro de Nitrome (id, cuadro en que suenan), sacados del propio runtime
INTRO_SOUNDS = [(497, 39), (497, 42), (497, 46), (485, 60)]
# Rojos de la paleta del juego (pañuelo/camiseta del equipo rojo)
RED_LIGHT, RED, RED_DARK, RED_DEEP = (0xf4, 0x4a, 0x3c), (0xe6, 0x30, 0x13), (0xc2, 0x22, 0x10), (0x8e, 0x16, 0x10)
OUTLINE = (0x2a, 0x22, 0x22)


def strip_outer_white(a):
    """Quita el contorno blanco exterior del sprite (sólo el blanco conectado con el fondo;
    la calavera del sombrero queda)."""
    h, w = a.shape[:2]
    outside = a[..., 3] == 0
    white = (a[..., 3] > 0) & (a[..., :3].min(axis=2) > 0xe0)
    grow = outside.copy()
    while True:
        n = grow.copy()
        n[1:] |= grow[:-1]
        n[:-1] |= grow[1:]
        n[:, 1:] |= grow[:, :-1]
        n[:, :-1] |= grow[:, 1:]
        n &= white | outside
        if (n == grow).all():
            break
        grow = n
    a = a.copy()
    a[grow & white] = 0
    return a


def make_icon():
    S = 48
    cx, cy = 23.5, 27.5
    yy, xx = np.mgrid[0:S, 0:S]
    ang = np.arctan2(yy - cy, xx - cx)
    ray = (np.floor((ang + math.pi) / (2 * math.pi) * 16) % 2).astype(bool)
    r = np.hypot(xx - cx, yy - cy)
    bg = np.zeros((S, S, 4), np.uint8)
    bg[...] = (*RED, 255)
    bg[ray] = (*RED_LIGHT, 255)
    bg[(r > 25) & ~ray] = (*RED_DARK, 255)
    bg[(r > 25) & ray] = (*RED, 255)
    bg[r > 31] = (*RED_DARK, 255)
    bg[r > 34] = (*RED_DEEP, 255)

    cap = strip_outer_white(load(EXT, CAPTAIN))
    ys, xs = np.where(cap[..., 3] > 0)
    cap = cap[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    cap = cap.repeat(2, 0).repeat(2, 1)
    ch, cw = cap.shape[:2]
    ox = (S - cw) // 2
    oy = 1                       # el sombrero entero arriba; el cuerpo se corta abajo
    img = bg.copy()
    m = np.zeros((S, S), bool)
    for y in range(ch):
        ty = oy + y
        if ty >= S:
            break
        for x in range(cw):
            tx = ox + x
            if 0 <= tx < S and cap[y, x, 3] > 0:
                img[ty, tx] = cap[y, x]
                m[ty, tx] = True
    # contorno oscuro de 1 px alrededor del capitán para separarlo de los rayos
    edge = np.zeros_like(m)
    edge[1:] |= m[:-1]
    edge[:-1] |= m[1:]
    edge[:, 1:] |= m[:, :-1]
    edge[:, :-1] |= m[:, 1:]
    edge &= ~m
    img[edge] = (*OUTLINE, 255)
    Image.fromarray(img, 'RGBA').save(os.path.join(OUT, 'icon.png'))
    return img


def make_banner():
    W, H = 256, 128
    logo = load(EXT, LOGO)
    lh, lw = logo.shape[:2]
    tw = W - 2
    th = round(lh * tw / lw)
    logo = drop_even(drop_even(logo, lw - tw, 1), lh - th, 0).astype(np.uint8)
    img = np.zeros((H, W, 4), np.uint8)
    ox, oy = (W - tw) // 2, (H - th) // 2
    img[oy:oy + th, ox:ox + tw] = logo
    Image.fromarray(img, 'RGBA').save(os.path.join(OUT, 'banner.png'))
    return img


def make_wav():
    """Mezcla los sonidos de la intro tal y como suenan en el juego (cuadros a 25 fps)."""
    inputs, filt = [], []
    for i, (sid, frame) in enumerate(INTRO_SOUNDS):
        inputs += ['-i', os.path.join(EXT, 'sounds', '%d.wav' % sid)]
        ms = round((frame - INTRO_SOUNDS[0][1]) * 1000 / 25)
        filt.append('[%d]aresample=32728,adelay=%d[a%d]' % (i, ms, i))
    mix = ''.join('[a%d]' % i for i in range(len(INTRO_SOUNDS)))
    filt.append('%samix=inputs=%d:normalize=0:duration=longest,alimiter=limit=0.95[out]' % (mix, len(INTRO_SOUNDS)))
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', *inputs, '-filter_complex', ';'.join(filt),
                    '-map', '[out]', '-ac', '1', '-ar', '32728', '-c:a', 'pcm_s16le',
                    os.path.join(OUT, 'banner.wav')], check=True)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    icon = make_icon()
    banner = make_banner()
    make_wav()
    # vista previa: banner sobre gris del menú HOME y el icono x3
    pv = Image.new('RGBA', (256 * 2 + 20 + 48 * 4, 256), (0xd8, 0xd8, 0xd8, 255))
    pv.alpha_composite(Image.fromarray(banner).resize((512, 256), Image.NEAREST), (0, 0))
    pv.alpha_composite(Image.fromarray(icon).resize((192, 192), Image.NEAREST), (532, 32))
    pv.save('/tmp/mutiny_work/banner_icon.png')
    print('ok', os.path.join(OUT, 'icon.png'), os.path.join(OUT, 'banner.png'))
