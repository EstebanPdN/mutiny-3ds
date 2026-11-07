#!/usr/bin/env python3
"""Genera los gráficos nuevos de la interfaz de dos pantallas, píxel a píxel, en assets/ui/.

Parten de los bitmaps originales cuando hay uno parecido (paneles), con la misma paleta, y se
dibujan a mano por código cuando no existen (tirachinas, botones táctiles, iconos).

Uso: tools/.venv/bin/python -I tools/gen_ui.py build/extract assets/ui
"""
import os
import sys

import numpy as np
from PIL import Image

OUTLINE = (0x2a, 0x22, 0x22)
PANEL = (0x30, 0x30, 0x30)
WHITE = (0xf8, 0xf8, 0xf8)


def load(ext, cid):
    return np.asarray(Image.open(os.path.join(ext, 'images', '%d.png' % cid)).convert('RGBA')).copy()


def save(out, name, arr):
    Image.fromarray(arr.astype(np.uint8), 'RGBA').save(os.path.join(out, name + '.png'))


def palette_map(src, dst):
    """Correspondencia de colores entre dos bitmaps con el mismo dibujo (panel rojo y azul)."""
    m = {}
    for a, b in zip(src.reshape(-1, 4), dst.reshape(-1, 4)):
        ka, kb = tuple(a), tuple(b)
        if ka != kb:
            m.setdefault(ka, {}).setdefault(kb, 0)
            m[ka][kb] += 1
    return {k: max(v.items(), key=lambda kv: kv[1])[0] for k, v in m.items()}


def recolor(arr, pmap):
    out = arr.copy()
    flat = out.reshape(-1, 4)
    for i, px in enumerate(flat):
        t = pmap.get(tuple(px))
        if t is not None:
            flat[i] = t
    return out


def trim_rows(arr, remove, lo, hi):
    """Quita `remove` filas repetidas (iguales a la anterior) entre lo y hi: encoge sin deformar."""
    rows = [r for r in range(max(lo, 1), hi) if np.array_equal(arr[r], arr[r - 1])]
    rows = rows[:remove]
    if len(rows) < remove:
        raise SystemExit('no hay %d filas repetidas entre %d y %d' % (remove, lo, hi))
    keep = [r for r in range(arr.shape[0]) if r not in set(rows)]
    return arr[keep]


def canvas(w, h):
    return np.zeros((h, w, 4), np.uint8)


def put(a, x, y, c, alpha=255):
    if 0 <= y < a.shape[0] and 0 <= x < a.shape[1]:
        a[y, x] = (*c, alpha)


def fill_rect(a, x0, y0, x1, y1, c):
    a[y0:y1, x0:x1] = (*c, 255)


def round_button(w, h, base, dark, light):
    """Botón con el estilo de los paneles: borde oscuro, cara plana, brillo arriba, sombra abajo."""
    a = canvas(w, h)
    r = 4
    for y in range(h):
        for x in range(w):
            dx = max(r - x, 0, x - (w - 1 - r))
            dy = max(r - y, 0, y - (h - 1 - r))
            if dx * dx + dy * dy > r * r:
                continue
            inner_dx = max(r - (x - 1), 0, (x + 1) - (w - 1 - r))
            inner_dy = max(r - (y - 1), 0, (y + 1) - (h - 1 - r))
            edge = (x == 0 or y == 0 or x == w - 1 or y == h - 1 or inner_dx * inner_dx + inner_dy * inner_dy > r * r)
            c = OUTLINE if edge else base
            if not edge:
                if y <= 2:
                    c = light
                elif y >= h - 4:
                    c = dark
            put(a, x, y, c)
    return a


def arrow_icon(w, h, direction, c):
    a = canvas(w + 2, h + 2)
    for y in range(h):
        half = min(y, h - 1 - y)
        for x in range(half + 1):
            xx = x if direction > 0 else w - 1 - x
            put(a, xx + 1, y + 1, c)
    alpha = a[..., 3] > 0
    edge = np.zeros_like(alpha)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        edge |= np.roll(np.roll(alpha, dy, 0), dx, 1)
    edge &= ~alpha
    a[edge] = (*OUTLINE, 255)
    return a


WOOD = (0xc0, 0x78, 0x4c)
WOOD_MID = (0xa7, 0x68, 0x41)
WOOD_DK = (0x8d, 0x53, 0x20)
WOOD_DEEP = (0x6a, 0x3a, 0x16)
WOOD_HI = (0xd4, 0x86, 0x33)


def deck_background(w=320, h=240, seed=7):
    """Cubierta de barco: tablones horizontales de 16 px con la paleta de los barcos del juego,
    juntas escalonadas, clavos y vetas. Bordes oscurecidos en dos pasos para dar profundidad."""
    rng = np.random.RandomState(seed)
    a = canvas(w, h)
    ph = 16
    for row in range(0, h, ph):
        r = row // ph
        fill_rect(a, 0, row, w, min(row + ph, h), WOOD)
        fill_rect(a, 0, row, w, row + 1, WOOD_HI)
        if row + ph - 1 < h:
            fill_rect(a, 0, row + ph - 1, w, row + ph, WOOD_DK)
        if row + ph - 2 < h:
            fill_rect(a, 0, row + ph - 2, w, row + ph - 1, WOOD_MID)
        # vetas
        for _ in range(9):
            gy = row + 3 + rng.randint(0, ph - 6)
            gx = rng.randint(-10, w)
            L = rng.randint(8, 34)
            fill_rect(a, max(gx, 0), gy, min(gx + L, w), gy + 1, WOOD_MID)
            if rng.rand() < 0.35:
                kx = max(gx + L // 2, 0)
                fill_rect(a, min(kx, w - 3), gy - 1, min(kx + 3, w), gy, WOOD_MID)
        # juntas escalonadas con clavos
        off = (r * 53) % 96
        for jx in range(off - 96, w + 96, 96 + (r % 3) * 16):
            if 0 <= jx < w:
                fill_rect(a, jx, row + 1, jx + 1, min(row + ph - 1, h), WOOD_DK)
                if jx + 1 < w:
                    fill_rect(a, jx + 1, row + 1, jx + 2, min(row + ph - 1, h), WOOD_HI)
                for nx in (jx - 4, jx + 3):
                    for ny in (row + 4, row + ph - 6):
                        if 0 <= nx < w - 1 and ny + 1 < h:
                            fill_rect(a, nx, ny, nx + 2, ny + 2, WOOD_DEEP)
                            put(a, nx, ny, WOOD_MID)
    # viñeta escalonada (sin degradados suaves: dos bandas de sombra)
    shade = np.ones((h, w))
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy))
    shade[d < 6] = 0.86
    shade[d < 3] = 0.74
    a[..., :3] = (a[..., :3] * shade[..., None]).astype(np.uint8)
    return a


# Iconos de la rejilla de armas con el estilo de los originales (marco 18x17 de 'blank',
# silueta gris claro #dbdbdb con sombra #adadad y detalles #4c4c4c), dibujados píxel a píxel.
ICON_PAL = {'#': (0xdb, 0xdb, 0xdb), '+': (0xad, 0xad, 0xad), '-': (0x4c, 0x4c, 0x4c), 'o': (0x31, 0x31, 0x31)}
WEAPON_ICONS = {
 'throw': [
  "................",
  ".....######.....",
  "...##########...",
  "..############.#",
  "..############++",
  "..############.+",
  "..++++++++++++..",
  "..##---####-##..",
  "..##---#######..",
  "..###-########..",
  "..############..",
  "...####--####...",
  "....########....",
  "......####......",
  "................"],
 'flag': [
  "................",
  "...+............",
  "...##########...",
  "...###########..",
  "...############.",
  "...###########+.",
  "...##########+..",
  "...#+++++++++...",
  "...#............",
  "...#............",
  "...#............",
  "...#............",
  "...#............",
  "..###...........",
  "................"],
 'close': [
  "................",
  "................",
  "...##......##...",
  "...###....###...",
  "....###..###....",
  ".....######.....",
  "......####......",
  "......####......",
  ".....######.....",
  "....###++###....",
  "...###+..+###...",
  "...##+....+##...",
  "................",
  "................",
  "................"],
}

def weapon_icon(name, ext):
    a = load(ext, 47).copy()
    for y, row in enumerate(WEAPON_ICONS[name]):
        for x, ch in enumerate(row):
            if ch in ICON_PAL:
                a[1 + y, 1 + x] = (*ICON_PAL[ch], 255)
    return a


def drop_even(a, n, axis):
    """Encoge pixel art quitando n columnas (axis=1) o filas (axis=0) COMPLETAS repartidas en
    tramos iguales: en cada tramo se quita la que más se parece a su vecina, nunca dos seguidas.
    Así cada letra encoge en la misma proporción (no se deforma), los bordes rectos siguen rectos
    y no hay remuestreo: cada píxel del resultado es un píxel del original."""
    if axis == 0:
        return drop_even(a.transpose(1, 0, 2), n, 1).transpose(1, 0, 2)
    a = a.astype(np.int32)
    w = a.shape[1]
    d_prev = np.concatenate(([1e18], np.abs(a[:, 1:] - a[:, :-1]).sum(axis=(0, 2)).astype(float)))
    d_next = np.concatenate((np.abs(a[:, :-1] - a[:, 1:]).sum(axis=(0, 2)).astype(float), [1e18]))
    cost = np.minimum(d_prev, d_next)
    edges = np.linspace(0, w, n + 1)
    removed = np.zeros(w, bool)
    for k in range(n):
        lo, hi = int(round(edges[k])), int(round(edges[k + 1]))
        if hi <= lo:
            continue
        seg = cost[lo:hi].copy()
        for j in range(lo, hi):
            if j > 0 and removed[j - 1]:
                seg[j - lo] += 1e15
        removed[lo + int(np.argmin(seg))] = True
    return a[:, ~removed].astype(np.uint8)


def main():
    ext, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    red, blue = load(ext, 1854), load(ext, 1856)
    # Panel de armas: 247 -> 240 filas quitando filas repetidas dentro del recuadro de descripción
    save(out, 'wpanel_red', trim_rows(red, 7, 190, 240))
    save(out, 'wpanel_blue', trim_rows(blue, 7, 190, 240))
    # Ventana emergente azul (el original solo la trae en rojo)
    pmap = palette_map(red, blue)
    save(out, 'popup_blue', recolor(load(ext, 410), pmap))
    save(out, 'bg_deck', deck_background())
    for name in ('throw', 'flag', 'close'):
        save(out, 'icon_' + name, weapon_icon(name, ext))
    # Icono de la 3DS (48x48): pirata rojo del juego a x2 sobre el cielo del menú, con borde
    pir = load(ext, 844)
    icon = canvas(48, 48)
    icon[...] = (0x8a, 0xcc, 0xe8, 255)
    icon[30:, :] = (0x2f, 0x68, 0xb4, 255)
    icon[29, :] = (0xb8, 0xe0, 0xf8, 255)
    big = np.repeat(np.repeat(pir, 2, axis=0), 2, axis=1)
    ph, pw = big.shape[:2]
    if pw > 44 or ph > 44:
        big = pir
        ph, pw = big.shape[:2]
    ox, oy = (48 - pw) // 2, (48 - ph) // 2 + 2
    m = big[..., 3] > 0
    icon[oy:oy + ph, ox:ox + pw][m] = big[m]
    icon[0, :] = icon[-1, :] = (*OUTLINE, 255)
    icon[:, 0] = icon[:, -1] = (*OUTLINE, 255)
    os.makedirs(os.path.join(os.path.dirname(out.rstrip('/')), '3ds'), exist_ok=True)
    Image.fromarray(icon, 'RGBA').save(os.path.join(os.path.dirname(out.rstrip('/')), '3ds', 'icon.png'))
    # Logo MUTINY: 452x154 -> 316x108 (un 30 % menos) quitando filas y columnas repartidas
    logo = load(ext, 1921)
    small = drop_even(drop_even(logo, logo.shape[1] - 316, 1), logo.shape[0] - 108, 0)
    save(out, 'logo', np.ascontiguousarray(small))
    teams = {
        'red': ((0xe6, 0x30, 0x13), (0xb5, 0x19, 0x0f), (0xf2, 0x86, 0x74)),
        'blue': ((0x36, 0x34, 0xd0), (0x1e, 0x1c, 0xa3), (0x88, 0x87, 0xe5)),
        'gray': ((0x58, 0x58, 0x58), (0x40, 0x40, 0x40), (0x80, 0x80, 0x80)),
        'gold': ((0xf0, 0xb0, 0x20), (0xc0, 0x80, 0x10), (0xff, 0xe0, 0x80)),
    }
    for name, (b, d, l) in teams.items():
        save(out, 'btn_%s' % name, round_button(96, 30, b, d, l))
        save(out, 'btn_%s_small' % name, round_button(56, 26, b, d, l))
        save(out, 'btn_%s_tiny' % name, round_button(40, 20, b, d, l))
        save(out, 'btn_%s_big' % name, round_button(148, 40, b, d, l))
    save(out, 'arrow_left', arrow_icon(14, 25, -1, WHITE))
    save(out, 'arrow_right', arrow_icon(14, 25, 1, WHITE))
    print('[gen_ui] %d imágenes en %s' % (len([f for f in os.listdir(out) if f.endswith('.png')]), out))


if __name__ == '__main__':
    main()
