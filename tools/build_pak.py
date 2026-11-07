#!/usr/bin/env python3
"""Convierte la extracción de Mutiny.swf (tools/extract.sh) en data/mutiny.pak.

Uso: tools/.venv/bin/python -I tools/build_pak.py build/extract assets/ui data/mutiny.pak

Formato (little-endian), leído por src/core/pak.c:
  cabecera: "MUTP" u32 versión u32 nsecciones, y por sección {char id[4], u32 off, u32 tamaño, u32 crudo, u32 zlib}
  STR  cadenas terminadas en NUL, referidas por desplazamiento
  PAGE u32 n, por página {u16 w, u16 h, u32 off}; después los píxeles RGBA8
  QUAD {u16 page, u16 u, v, w, h; i16 dx, dy; u16 0}: trozo de página dibujado a 1:1 en (dx, dy)
  CHAR por id de carácter {u8 tipo, u8 0, u16 n, u32 off en CDAT}
  CDAT datos de cada carácter (ver write_char)
  OPS  operaciones de línea de tiempo de 64 bytes
  PRMS parámetros de las fuentes de mapa de bits colocadas en el escenario
  LINK {u32 nombre, u16 id, u16 0} ordenado por nombre
  SND  PCM 16 bits mono
  LVL  niveles
"""
import hashlib
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

import numpy as np
from PIL import Image

PAGE = 512
MAXIMG = 510

K_NONE, K_SHAPE, K_MORPH, K_SPRITE, K_SOUND = 0, 1, 2, 3, 4
OP_PLACE, OP_REMOVE, OP_ACTION, OP_SOUND = 1, 2, 3, 4
PF_MOVE, PF_CHAR, PF_MATRIX, PF_CX, PF_NAME, PF_RATIO, PF_CLIP = 1, 2, 4, 8, 16, 32, 64
A_STOP, A_PLAY, A_GOTO_PLAY, A_GOTO_STOP, A_DESTROY, A_CALL = 1, 2, 3, 4, 5, 6
UI_BASE_ID = 5000
OP_FMT = '<BBHHHffffff8hIhHII'  # 64 bytes


def pack_op(*v):
    return struct.pack(OP_FMT, *v, 0)


class Strings:
    def __init__(self):
        self.blob = bytearray(b'\0')
        self.index = {'': 0}

    def add(self, s):
        if s is None:
            return 0
        if s not in self.index:
            self.index[s] = len(self.blob)
            self.blob += s.encode('utf-8') + b'\0'
        return self.index[s]


def bitmap_png(ext, cid):
    p = os.path.join(ext, 'images', '%d.png' % cid)
    return np.asarray(Image.open(p).convert('RGBA'))


def shape_png(ext, cid):
    p = os.path.join(ext, 'shapes', '%d.png' % cid)
    if not os.path.exists(p):
        return None
    return np.asarray(Image.open(p).convert('RGBA'))


def rect(el):
    return [int(el.get(k)) for k in ('Xmin', 'Ymin', 'Xmax', 'Ymax')]


# ---------------------------------------------------------------- formas
def parse_styles(styles_el):
    fills, lines = [], []
    if styles_el is None:
        return fills, lines
    fs = styles_el.find('fillStyles')
    if fs is not None:
        fs = fs.find('fillStyles') if fs.find('fillStyles') is not None else fs
        for it in fs:
            fills.append(it)
    ls = styles_el.find('lineStyles')
    if ls is not None:
        inner = [c for c in ls if c.tag in ('lineStyles', 'lineStyles2')]
        for c in (inner if inner else [ls]):
            for it in c:
                lines.append(it)
    return fills, lines


def shape_paths(records, fills, lines):
    """Recorre los registros y devuelve tramos [(fill0, fill1, line, [(x,y),...])] en twips,
    con las curvas aplanadas, más las listas de estilos activas en cada tramo."""
    x = y = 0
    f0 = f1 = ln = 0
    cur = None
    out = []
    style_sets = [(fills, lines)]
    sidx = 0
    curves = False
    for r in records:
        t = r.get('type')
        if t == 'StyleChangeRecord':
            if r.get('stateNewStyles') == 'true':
                nf, nl = parse_styles(r)
                style_sets.append((nf, nl))
                sidx = len(style_sets) - 1
            if r.get('stateMoveTo') == 'true':
                x = int(r.get('moveDeltaX', 0))
                y = int(r.get('moveDeltaY', 0))
            if r.get('stateFillStyle0') == 'true':
                f0 = int(r.get('fillStyle0'))
            if r.get('stateFillStyle1') == 'true':
                f1 = int(r.get('fillStyle1'))
            if r.get('stateLineStyle') == 'true':
                ln = int(r.get('lineStyle'))
            cur = None
        elif t == 'StraightEdgeRecord':
            if cur is None:
                cur = (sidx, f0, f1, ln, [(x, y)])
                out.append(cur)
            if r.get('generalLineFlag') == 'true':
                x += int(r.get('deltaX', 0))
                y += int(r.get('deltaY', 0))
            elif r.get('vertLineFlag') == 'true':
                y += int(r.get('deltaY', 0))
            else:
                x += int(r.get('deltaX', 0))
            cur[4].append((x, y))
        elif t == 'CurvedEdgeRecord':
            curves = True
            if cur is None:
                cur = (sidx, f0, f1, ln, [(x, y)])
                out.append(cur)
            cx = x + int(r.get('controlDeltaX'))
            cy = y + int(r.get('controlDeltaY'))
            ax = cx + int(r.get('anchorDeltaX'))
            ay = cy + int(r.get('anchorDeltaY'))
            n = 8
            for i in range(1, n + 1):
                tt = i / n
                px = (1 - tt) ** 2 * x + 2 * (1 - tt) * tt * cx + tt * tt * ax
                py = (1 - tt) ** 2 * y + 2 * (1 - tt) * tt * cy + tt * tt * ay
                cur[4].append((px, py))
            x, y = ax, ay
    return out, style_sets, curves


def try_bitmap_decompose(el, ext, png, ox, oy):
    """Formas que son solo bitmaps alineados: devuelve [(bitmap, sx, sy, w, h, dx, dy)] o None.
    Con escala exacta 20 se comprueba contra el PNG de JPEXS píxel a píxel. Si el artista estiró el
    bitmap menos de un píxel (escala 19,99x) se usa el bitmap original sin estirar y se exige solo
    parecido medio, porque JPEXS remuestrea."""
    shapes = el.find('shapes')
    fills, lines = parse_styles(shapes)
    if lines:
        return None
    records = shapes.find('shapeRecords')
    paths, style_sets, curves = shape_paths(records, fills, lines)
    if curves:
        return None
    for fs, ls in style_sets:
        if ls:
            return None
        for f in fs:
            if int(f.get('fillStyleType')) not in (0x40, 0x41, 0x42, 0x43):
                return None
    boxes = {}
    for sidx, f0, f1, ln, pts in paths:
        for s in (f0, f1):
            if s:
                b = boxes.setdefault((sidx, s), [1e9, 1e9, -1e9, -1e9])
                for (px, py) in pts:
                    b[0] = min(b[0], px); b[1] = min(b[1], py)
                    b[2] = max(b[2], px); b[3] = max(b[3], py)
    H, W = png.shape[:2]
    cand = np.zeros((H, W, 4), np.uint8)
    quads = []
    exact = True
    for (sidx, s), b in sorted(boxes.items()):
        f = style_sets[sidx][0][s - 1]
        bid = int(f.get('bitmapId'))
        if bid == 65535:
            continue
        m = f.find('bitmapMatrix')
        sx_, sy_ = float(m.get('scaleX', 20)), float(m.get('scaleY', 20))
        if m.get('hasRotate') == 'true' or abs(sx_ - 20) > 0.02 or abs(sy_ - 20) > 0.02:
            return None
        if sx_ != 20.0 or sy_ != 20.0:
            exact = False
        tx, ty = int(m.get('translateX', 0)), int(m.get('translateY', 0))
        if tx % 20 or ty % 20 or any(int(v) % 20 for v in b):
            exact = False
        bx, by = round(tx / 20), round(ty / 20)
        rx0, ry0, rx1, ry1 = (round(v / 20) for v in b)
        bmp = bitmap_png(ext, bid)
        bh, bw = bmp.shape[:2]
        repeat = int(f.get('fillStyleType')) in (0x40, 0x42)
        if not repeat:
            rx0, ry0 = max(rx0, bx), max(ry0, by)
            rx1, ry1 = min(rx1, bx + bw), min(ry1, by + bh)
            if rx1 <= rx0 or ry1 <= ry0:
                continue
            tiles = [(rx0 - bx, ry0 - by, rx1 - rx0, ry1 - ry0, rx0, ry0)]
        else:
            tiles = []
            y = ry0
            while y < ry1:
                sy = (y - by) % bh
                h = min(bh - sy, ry1 - y)
                x = rx0
                while x < rx1:
                    sx = (x - bx) % bw
                    w = min(bw - sx, rx1 - x)
                    tiles.append((sx, sy, w, h, x, y))
                    x += w
                y += h
        for (sx, sy, w, h, dx, dy) in tiles:
            cx0, cy0 = dx - ox, dy - oy
            w = min(w, W - cx0)
            h = min(h, H - cy0)
            if cx0 < 0 or cy0 < 0 or w <= 0 or h <= 0:
                return None
            src = bmp[sy:sy + h, sx:sx + w]
            dst = cand[cy0:cy0 + h, cx0:cx0 + w]
            mask = src[..., 3] > 0
            dst[mask] = src[mask]
            quads.append((('bmp', bid), sx, sy, w, h, dx, dy))
    diff = np.abs(cand.astype(int) - png.astype(int))
    if exact:
        opaque = png[..., 3] == 255
        if diff[opaque].max(initial=0) > 0 or diff.max(initial=0) > 3:
            return None
    elif diff.mean() > 12:
        return None
    return quads


def gradient_color(grad, t):
    recs = grad
    t = max(0.0, min(255.0, t))
    if t <= recs[0][0]:
        return recs[0][1]
    for (r0, c0), (r1, c1) in zip(recs, recs[1:]):
        if t <= r1:
            k = (t - r0) / max(1e-6, r1 - r0)
            return tuple(c0[i] + (c1[i] - c0[i]) * k for i in range(4))
    return recs[-1][1]


def color_of(el):
    return (int(el.get('red')), int(el.get('green')), int(el.get('blue')), int(el.get('alpha', 255)))


BITMAP_CACHE = {}
EXT_DIR = None


def bitmap_sampler(f):
    """Relleno de bitmap: muestreo al vecino más cercano a través de la matriz inversa,
    repitiendo (0x40/0x42) o fijando el borde (0x41/0x43), como Flash en calidad baja."""
    ft = int(f.get('fillStyleType'))
    bid = int(f.get('bitmapId'))
    if bid == 65535:
        return None
    if bid not in BITMAP_CACHE:
        BITMAP_CACHE[bid] = bitmap_png(EXT_DIR, bid).astype(float)
    bmp = BITMAP_CACHE[bid]
    bh, bw = bmp.shape[:2]
    m = f.find('bitmapMatrix')
    a = float(m.get('scaleX', 1)) if m.get('hasScale') == 'true' else 1.0
    d = float(m.get('scaleY', 1)) if m.get('hasScale') == 'true' else 1.0
    b = float(m.get('rotateSkew0', 0)) if m.get('hasRotate') == 'true' else 0.0
    c = float(m.get('rotateSkew1', 0)) if m.get('hasRotate') == 'true' else 0.0
    tx, ty = float(m.get('translateX', 0)), float(m.get('translateY', 0))
    det = a * d - b * c
    if abs(det) < 1e-12:
        return None
    ia, ib, ic, id_ = d / det, -b / det, -c / det, a / det

    def samp(xs, ys):
        px, py = xs - tx, ys - ty
        u = np.floor(ia * px + ic * py).astype(int)
        v = np.floor(ib * px + id_ * py).astype(int)
        if ft in (0x40, 0x42):
            u %= bw
            v %= bh
        else:
            u = np.clip(u, 0, bw - 1)
            v = np.clip(v, 0, bh - 1)
        return bmp[v, u]
    return samp


def fill_sampler(f, ratio=None, morph=False):
    """Devuelve una función (xs, ys en twips) -> RGBA float para un estilo de relleno."""
    ft = int(f.get('fillStyleType'))
    if 0x40 <= ft <= 0x43 and not morph:
        return bitmap_sampler(f)
    lerp = (lambda a, b: a + (b - a) * ratio) if morph else None
    if ft == 0:
        if morph:
            c0, c1 = color_of(f.find('startColor')), color_of(f.find('endColor'))
            c = tuple(lerp(c0[i], c1[i]) for i in range(4))
        else:
            c = color_of(f.find('color'))
        return lambda xs, ys: np.broadcast_to(np.array(c, float), xs.shape + (4,))
    if ft in (0x10, 0x12, 0x13):
        if morph:
            m0, m1 = f.find('startGradientMatrix'), f.find('endGradientMatrix')
            recs = []
            for g in f.find('gradient').find('gradientRecords'):
                r = lerp(int(g.get('startRatio')), int(g.get('endRatio')))
                c0, c1 = color_of(g.find('startColor')), color_of(g.find('endColor'))
                recs.append((r, tuple(lerp(c0[i], c1[i]) for i in range(4))))
            mm = [lerp(*(float(m.get(k, 0)) for m in (m0, m1))) for k in ('scaleX', 'rotateSkew0', 'rotateSkew1', 'scaleY', 'translateX', 'translateY')]
            for i, k in enumerate(('scaleX', 'scaleY')):
                pass
            a, b, c, d, tx, ty = mm
            if m0.get('hasScale') != 'true':
                a = d = 1.0
        else:
            m = f.find('gradientMatrix')
            recs = []
            for g in f.find('gradient').find('gradientRecords'):
                recs.append((int(g.get('ratio')), color_of(g.find('color'))))
            a = float(m.get('scaleX', 1)) if m.get('hasScale') == 'true' else 1.0
            d = float(m.get('scaleY', 1)) if m.get('hasScale') == 'true' else 1.0
            b = float(m.get('rotateSkew0', 0)) if m.get('hasRotate') == 'true' else 0.0
            c = float(m.get('rotateSkew1', 0)) if m.get('hasRotate') == 'true' else 0.0
            tx, ty = float(m.get('translateX', 0)), float(m.get('translateY', 0))
        det = a * d - b * c
        if abs(det) < 1e-12:
            det = 1e-12
        ia, ib, ic, id_ = d / det, -b / det, -c / det, a / det

        def samp(xs, ys):
            px, py = xs - tx, ys - ty
            gx = ia * px + ic * py
            gy = ib * px + id_ * py
            if ft == 0x10:
                t = (gx + 16384) / 32768 * 255
            else:
                t = np.sqrt(gx * gx + gy * gy) / 16384 * 255
            out = np.zeros(xs.shape + (4,), float)
            rs = np.array([r for r, _ in recs], float)
            cs = np.array([cc for _, cc in recs], float)
            tt = np.clip(t, 0, 255)
            for i in range(4):
                out[..., i] = np.interp(tt, rs, cs[:, i])
            return out
        return samp
    return None


def point_in_paths(xs, ys, polys):
    """Paridad par-impar de cruces horizontales, vectorizada."""
    inside = np.zeros(xs.shape, bool)
    for pts in polys:
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if y0 == y1:
                continue
            cond = (ys >= min(y0, y1)) & (ys < max(y0, y1))
            xi = x0 + (ys - y0) * (x1 - x0) / (y1 - y0)
            inside ^= cond & (xs < xi)
    return inside


def seg_dist(xs, ys, pts):
    best = np.full(xs.shape, 1e18)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        dx, dy = x1 - x0, y1 - y0
        L = dx * dx + dy * dy
        if L == 0:
            t = np.zeros(xs.shape)
        else:
            t = np.clip(((xs - x0) * dx + (ys - y0) * dy) / L, 0, 1)
        qx, qy = x0 + t * dx, y0 + t * dy
        best = np.minimum(best, (xs - qx) ** 2 + (ys - qy) ** 2)
    return np.sqrt(best)


def rasterize(paths, style_sets, ox, oy, W, H, morph_ratio=None):
    """Rasteriza sin suavizado (como _quality LOW) muestreando el centro de cada píxel."""
    img = np.zeros((H, W, 4), float)
    if W <= 0 or H <= 0:
        return img.astype(np.uint8)
    gy, gx = np.mgrid[0:H, 0:W]
    xs = (gx + ox + 0.5) * 20.0
    ys = (gy + oy + 0.5) * 20.0
    morph = morph_ratio is not None

    def over(rgba, mask):
        a = rgba[..., 3:4] / 255.0 * mask[..., None]
        img[..., :3] = rgba[..., :3] * a + img[..., :3] * (1 - a)
        img[..., 3:4] = 255 * a + img[..., 3:4] * (1 - a)

    for sidx, (fills, lines) in enumerate(style_sets):
        for fi, f in enumerate(fills, 1):
            polys = [p for (s, f0, f1, ln, p) in paths if s == sidx and (f0 == fi or f1 == fi)]
            if not polys:
                continue
            samp = fill_sampler(f, morph_ratio, morph)
            if samp is None:
                continue
            mask = point_in_paths(xs, ys, polys)
            if mask.any():
                over(samp(xs, ys), mask.astype(float))
        for li, l in enumerate(lines, 1):
            polys = [p for (s, f0, f1, ln, p) in paths if s == sidx and ln == li]
            if not polys:
                continue
            if morph:
                w = int(l.get('startWidth')) + (int(l.get('endWidth')) - int(l.get('startWidth'))) * morph_ratio
                c0, c1 = color_of(l.find('startColor')), color_of(l.find('endColor'))
                col = tuple(c0[i] + (c1[i] - c0[i]) * morph_ratio for i in range(4))
            else:
                w = int(l.get('width'))
                col = color_of(l.find('color'))
            if col[3] == 0:
                continue
            half = max(w / 2.0, 10.0)
            mask = np.zeros(xs.shape, bool)
            for p in polys:
                mask |= seg_dist(xs, ys, p) <= half
            over(np.broadcast_to(np.array(col, float), xs.shape + (4,)), mask.astype(float))
    return np.clip(img + 0.5, 0, 255).astype(np.uint8)


def shape_bounds_px(b):
    x0, y0, x1, y1 = b
    ox, oy = math.floor(x0 / 20), math.floor(y0 / 20)
    return ox, oy, math.ceil(x1 / 20) - ox, math.ceil(y1 / 20) - oy


def morph_paths(el, ratio):
    """Interpola los bordes de inicio y fin de una forma morph (registro a registro)."""
    fills, lines = [], []
    mf = el.find('morphFillStyles')
    if mf is not None:
        fills = [it for c in mf for it in c]
    ml = el.find('morphLineStyles')
    if ml is not None:
        lines = [it for c in ml for it in c]
    s_recs = list(el.find('startEdges').find('shapeRecords'))
    e_recs = [r for r in el.find('endEdges').find('shapeRecords')]
    e_edges = [r for r in e_recs if r.get('type') in ('StraightEdgeRecord', 'CurvedEdgeRecord')]
    e_moves = [r for r in e_recs if r.get('type') == 'StyleChangeRecord' and r.get('stateMoveTo') == 'true']

    def edge_pts(r, x, y):
        if r.get('type') == 'StraightEdgeRecord':
            if r.get('generalLineFlag') == 'true':
                return [(x + int(r.get('deltaX', 0)), y + int(r.get('deltaY', 0)))], None
            if r.get('vertLineFlag') == 'true':
                return [(x, y + int(r.get('deltaY', 0)))], None
            return [(x + int(r.get('deltaX', 0)), y)], None
        cx = x + int(r.get('controlDeltaX')); cy = y + int(r.get('controlDeltaY'))
        ax = cx + int(r.get('anchorDeltaX')); ay = cy + int(r.get('anchorDeltaY'))
        return [(ax, ay)], (cx, cy)

    out = []
    sx = sy = ex = ey = 0
    f0 = f1 = ln = 0
    cur = None
    ei = mi = 0
    for r in s_recs:
        t = r.get('type')
        if t == 'StyleChangeRecord':
            if r.get('stateMoveTo') == 'true':
                sx, sy = int(r.get('moveDeltaX', 0)), int(r.get('moveDeltaY', 0))
                if mi < len(e_moves):
                    ex, ey = int(e_moves[mi].get('moveDeltaX', 0)), int(e_moves[mi].get('moveDeltaY', 0))
                    mi += 1
            if r.get('stateFillStyle0') == 'true':
                f0 = int(r.get('fillStyle0'))
            if r.get('stateFillStyle1') == 'true':
                f1 = int(r.get('fillStyle1'))
            if r.get('stateLineStyle') == 'true':
                ln = int(r.get('lineStyle'))
            cur = None
        elif t in ('StraightEdgeRecord', 'CurvedEdgeRecord'):
            if cur is None:
                cur = (0, f0, f1, ln, [(sx + (ex - sx) * ratio, sy + (ey - sy) * ratio)])
                out.append(cur)
            sp, sc = edge_pts(r, sx, sy)
            er = e_edges[ei] if ei < len(e_edges) else r
            ei += 1
            ep, ec = edge_pts(er, ex, ey)
            nsx, nsy = sp[0]
            nex, ney = ep[0]
            if sc is None and ec is None:
                cur[4].append((nsx + (nex - nsx) * ratio, nsy + (ney - nsy) * ratio))
            else:
                scx, scy = sc if sc else ((sx + nsx) / 2, (sy + nsy) / 2)
                ecx, ecy = ec if ec else ((ex + nex) / 2, (ey + ney) / 2)
                x0, y0 = sx + (ex - sx) * ratio, sy + (ey - sy) * ratio
                cx_, cy_ = scx + (ecx - scx) * ratio, scy + (ecy - scy) * ratio
                ax, ay = nsx + (nex - nsx) * ratio, nsy + (ney - nsy) * ratio
                for i in range(1, 9):
                    tt = i / 8
                    cur[4].append(((1 - tt) ** 2 * x0 + 2 * (1 - tt) * tt * cx_ + tt * tt * ax,
                                   (1 - tt) ** 2 * y0 + 2 * (1 - tt) * tt * cy_ + tt * tt * ay))
            sx, sy, ex, ey = nsx, nsy, nex, ney
    return out, [(fills, lines)]


# ---------------------------------------------------------------- acciones
def parse_actions(text, labels):
    acts = []
    raw = []
    body = re.sub(r'//.*', '', text)
    for st in re.split(r'[;\n]', body):
        st = st.strip()
        if not st or st in ('{', '}'):
            continue
        s = re.sub(r'^(this\.)', '', st)
        m = re.match(r'^(gotoAndPlay|gotoAndStop)\((.+)\)$', s)
        if s in ('stop()',):
            acts.append((A_STOP, 0, None))
        elif s in ('play()',):
            acts.append((A_PLAY, 0, None))
        elif m:
            arg = m.group(2).strip()
            if arg.startswith('"'):
                lab = arg.strip('"').lower()
                fr = labels.get(lab)
                if fr is None:
                    raw.append(st)
                    acts.append((A_CALL, 0, st))
                    continue
            else:
                fr = int(arg)
            acts.append((A_GOTO_PLAY if m.group(1) == 'gotoAndPlay' else A_GOTO_STOP, fr, None))
        elif s == 'cl.destroy()':
            acts.append((A_DESTROY, 0, None))
        else:
            acts.append((A_CALL, 0, st))
            raw.append(st)
    return acts, raw


def parse_construct(text):
    p = {}
    for k, v in re.findall(r'(\w+)\s*=\s*("(?:[^"\\]|\\.)*"|[^;\n]+);', text):
        v = v.strip()
        if v.startswith('"'):
            v = v[1:-1].replace('\\"', '"').replace("\\'", "'")
        p[k] = v
    return p


# ---------------------------------------------------------------- empaquetado
class Packer:
    def __init__(self):
        self.pages = []
        self.shelves = []

    def new_page(self):
        self.pages.append(np.zeros((PAGE, PAGE, 4), np.uint8))
        self.shelves.append([0, 0, 0])  # y de la balda, x actual, alto de la balda
        return len(self.pages) - 1

    def place(self, img):
        h, w = img.shape[:2]
        cw, ch = w + 2, h + 2
        for pi in range(max(0, len(self.pages) - 2), len(self.pages)):
            r = self._fit(pi, cw, ch)
            if r:
                return self._blit(pi, r, img)
        pi = self.new_page()
        r = self._fit(pi, cw, ch)
        assert r, (w, h)
        return self._blit(pi, r, img)

    def _fit(self, pi, cw, ch):
        sy, sx, sh = self.shelves[pi]
        if sx + cw <= PAGE and sy + max(sh, ch) <= PAGE and (ch <= sh or sx == 0):
            if sx == 0:
                self.shelves[pi][2] = max(sh, ch)
            x, y = sx, sy
            self.shelves[pi][1] = sx + cw
            return (x, y)
        ny = sy + sh
        if ny + ch <= PAGE and cw <= PAGE:
            self.shelves[pi] = [ny, cw, ch]
            return (0, ny)
        return None

    def _blit(self, pi, r, img):
        x, y = r
        h, w = img.shape[:2]
        self.pages[pi][y + 1:y + 1 + h, x + 1:x + 1 + w] = img
        return pi, x + 1, y + 1


TARGET_3DS = False


def tile_3ds(pg):
    """RGBA8 (filas) -> textura RGBA8 de la 3DS: bytes A,B,G,R en baldosas Morton 8x8.
    La primera fila en memoria es la de arriba (t = 1); el muestreo de la GPU invierte t."""
    h, w = pg.shape[:2]
    flipped = pg
    y, x = np.mgrid[0:h, 0:w]
    m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)
    idx = ((((y >> 3) * (w >> 3) + (x >> 3)) << 6) + m).ravel()
    out = np.zeros((w * h, 4), np.uint8)
    src = flipped.reshape(-1, 4)
    out[idx] = src[:, [3, 2, 1, 0]]
    return out.tobytes()


def main():
    global TARGET_3DS
    TARGET_3DS = '--3ds' in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    ext, uidir, out = args[0], args[1], args[2]
    global EXT_DIR
    EXT_DIR = ext
    root = ET.parse(os.path.join(ext, 'swf.xml')).getroot()
    tags = root.find('tags')
    S = Strings()
    chars, exports = {}, {}
    for t in tags:
        ty = t.get('type')
        cid = (t.get('shapeId') or t.get('spriteId') or t.get('characterID') or t.get('buttonId')
               or t.get('fontID') or t.get('fontId') or t.get('soundId') or t.get('characterId'))
        if ty.startswith('Define') and cid is not None:
            chars[int(cid)] = t
        if ty == 'ExportAssetsTag':
            for a, b in zip(t.find('tags'), t.find('names')):
                exports[int(a.text)] = b.text

    # --- imágenes fuente (bitmaps y rasterizados)
    images = {}          # clave -> np array
    char_quads = {}      # id -> (bounds px, [quads con clave])
    morph_entries = {}   # id -> [(ratio, bounds, quads)]
    report = {'bitmap_shapes': 0, 'raster_shapes': 0, 'vector_shapes': 0, 'empty_shapes': 0}

    def add_image(key, arr):
        images[key] = arr
        return key

    for cid, t in chars.items():
        ty = t.get('type')
        if ty not in ('DefineShapeTag', 'DefineShape2Tag', 'DefineShape3Tag', 'DefineShape4Tag'):
            continue
        b = rect(t.find('shapeBounds'))
        ox, oy, W, H = shape_bounds_px(b)
        bounds = (b[0] / 20, b[1] / 20, b[2] / 20, b[3] / 20)
        png = shape_png(ext, cid)
        fills, lines = parse_styles(t.find('shapes'))
        kinds = set(int(f.get('fillStyleType')) for f in fills)
        quads = None
        if png is not None and png.shape[0] == H and png.shape[1] == W:
            q = try_bitmap_decompose(t, ext, png, ox, oy)
            if q is not None:
                quads = []
                for (key, sx, sy, w, h, dx, dy) in q:
                    if key not in images:
                        add_image(key, bitmap_png(ext, key[1]))
                    quads.append((key, sx, sy, w, h, dx, dy))
                report['bitmap_shapes'] += 1
        if quads is None and not any(k >= 0x40 for k in kinds):
            paths, style_sets, _ = shape_paths(t.find('shapes').find('shapeRecords'), fills, lines)
            img = rasterize(paths, style_sets, ox, oy, W, H)
            if img[..., 3].any():
                key = add_image(('vec', cid), img)
                quads = [(key, 0, 0, W, H, ox, oy)]
            else:
                quads = []
            report['vector_shapes'] += 1
        if quads is None and any(k >= 0x40 for k in kinds):
            # Bitmaps estirados (paneles de 9 trozos, etc.): rasterizado propio sin juntas
            paths, style_sets, _ = shape_paths(t.find('shapes').find('shapeRecords'), fills, lines)
            img = rasterize(paths, style_sets, ox, oy, W, H)
            if img[..., 3].any():
                key = add_image(('bras', cid), img)
                quads = [(key, 0, 0, W, H, ox, oy)]
                report['raster_shapes'] += 1
        if quads is None:
            if png is None:
                quads = []
                report['empty_shapes'] += 1
            else:
                key = add_image(('ras', cid), png)
                quads = [(key, 0, 0, png.shape[1], png.shape[0], ox, oy)]
                report['raster_shapes'] += 1
        char_quads[cid] = (bounds, quads)

    # --- proporciones de morph usadas en las líneas de tiempo
    ratios = {}

    def scan_ratios(sub):
        at = {}
        for st in sub:
            if st.get('type') in ('PlaceObject2Tag', 'PlaceObject3Tag'):
                d = int(st.get('depth'))
                if st.get('characterId'):
                    at[d] = int(st.get('characterId'))
                ch = at.get(d)
                if ch in chars and chars[ch].get('type').startswith('DefineMorph'):
                    r = int(st.get('ratio')) if st.get('placeFlagHasRatio') == 'true' else 0
                    ratios.setdefault(ch, set()).add(r)
            elif st.get('type') == 'RemoveObject2Tag':
                at.pop(int(st.get('depth')), None)
    for cid, t in chars.items():
        if t.get('type') == 'DefineSpriteTag':
            scan_ratios(t.find('subTags'))
    scan_ratios(tags)
    for cid, t in chars.items():
        if not t.get('type').startswith('DefineMorph'):
            continue
        ents = []
        for r in sorted(ratios.get(cid, {0})):
            k = r / 65535.0
            sb, eb = rect(t.find('startBounds')), rect(t.find('endBounds'))
            bb = [sb[i] + (eb[i] - sb[i]) * k for i in range(4)]
            ox, oy, W, H = shape_bounds_px(bb)
            paths, style_sets = morph_paths(t, k)
            img = rasterize(paths, style_sets, ox, oy, W, H, morph_ratio=k)
            quads = []
            if img[..., 3].any():
                key = add_image(('morph', cid, r), img)
                quads = [(key, 0, 0, W, H, ox, oy)]
            ents.append((r, tuple(v / 20 for v in bb), quads))
        morph_entries[cid] = ents

    # --- gráficos nuevos de la interfaz (assets/ui/*.png), con linkage "ui_<nombre>"
    ui_ids = {}
    if os.path.isdir(uidir):
        nid = UI_BASE_ID
        for fn in sorted(os.listdir(uidir)):
            if not fn.endswith('.png'):
                continue
            arr = np.asarray(Image.open(os.path.join(uidir, fn)).convert('RGBA'))
            name = 'ui_' + fn[:-4]
            key = add_image(('ui', name), arr)
            h, w = arr.shape[:2]
            char_quads[nid] = ((0, 0, w, h), [(key, 0, 0, w, h, 0, 0)])
            ui_ids[name] = nid
            exports[nid] = name
            nid += 1

    # --- trocear imágenes grandes y deduplicar
    tiles = {}  # clave -> [(x0, y0, w, h, clave_tile)]
    uniq = {}
    for key, arr in images.items():
        h, w = arr.shape[:2]
        parts = []
        for y0 in range(0, h, MAXIMG):
            for x0 in range(0, w, MAXIMG):
                sub = np.ascontiguousarray(arr[y0:y0 + MAXIMG, x0:x0 + MAXIMG])
                hsh = hashlib.sha1(sub.tobytes() + str(sub.shape).encode()).hexdigest()
                uniq.setdefault(hsh, sub)
                parts.append((x0, y0, sub.shape[1], sub.shape[0], hsh))
        tiles[key] = parts
    # Agrupar por el símbolo que usa cada imagen: así una pantalla necesita pocas páginas
    # (la 3DS sube las páginas a memoria lineal solo cuando se dibujan).
    char_keys = {}

    def keys_of(cid, seen):
        if cid in char_keys:
            return char_keys[cid]
        if cid in seen:
            return set()
        seen.add(cid)
        ks = set()
        if cid in char_quads:
            ks |= {q[0] for q in char_quads[cid][1]}
        if cid in morph_entries:
            for (_, _, qs) in morph_entries[cid]:
                ks |= {q[0] for q in qs}
        t = chars.get(cid)
        if t is not None and t.get('type') == 'DefineSpriteTag':
            for st in t.find('subTags'):
                if st.get('characterId'):
                    ks |= keys_of(int(st.get('characterId')), seen)
        char_keys[cid] = ks
        return ks
    owners = {}
    for cid, name in exports.items():
        for k in keys_of(cid, set()):
            owners.setdefault(k, set()).add(name)
    for st in tags:
        if st.get('type') in ('PlaceObject2Tag', 'PlaceObject3Tag') and st.get('characterId'):
            for k in keys_of(int(st.get('characterId')), set()):
                owners.setdefault(k, set()).add('~root')
    hash_group = {}
    for key, parts in tiles.items():
        g = min(owners.get(key, {'~~sin_dueño'}))
        for p in parts:
            hash_group[p[4]] = min(hash_group.get(p[4], g), g)
    packer = Packer()
    placed = {}
    for hsh, sub in sorted(uniq.items(), key=lambda kv: (hash_group.get(kv[0], '~~'), -kv[1].shape[0])):
        placed[hsh] = packer.place(sub)

    quad_table = []

    def emit_quads(qs):
        first = len(quad_table)
        for (key, sx, sy, w, h, dx, dy) in qs:
            for (x0, y0, tw, th, hsh) in tiles[key]:
                ix0, iy0 = max(sx, x0), max(sy, y0)
                ix1, iy1 = min(sx + w, x0 + tw), min(sy + h, y0 + th)
                if ix1 <= ix0 or iy1 <= iy0:
                    continue
                pi, px, py = placed[hsh]
                quad_table.append((pi, px + ix0 - x0, py + iy0 - y0, ix1 - ix0, iy1 - iy0,
                                   dx + ix0 - sx, dy + iy0 - sy))
        return first, len(quad_table) - first

    # --- sprites
    ops = []
    params = []
    raw_calls = {}
    scripts_dir = os.path.join(ext, 'scripts')
    sprite_dirs = {}
    for d in os.listdir(scripts_dir):
        m = re.match(r'^DefineSprite_(\d+)', d)
        if m:
            sprite_dirs[int(m.group(1))] = os.path.join(scripts_dir, d)

    def read(p):
        with open(p, encoding='utf-8', errors='replace') as fh:
            return fh.read()

    def build_timeline(sub, nframes, sdir, sid):
        labels = {}
        f = 1
        for st in sub:
            if st.get('type') == 'ShowFrameTag':
                f += 1
            elif st.get('type') == 'FrameLabelTag':
                labels.setdefault(st.get('name').lower(), f)
        frame_ops = [[] for _ in range(nframes + 1)]
        f = 1
        for st in sub:
            ty = st.get('type')
            if ty == 'ShowFrameTag':
                f += 1
                if f > nframes:
                    break
                continue
            if ty in ('PlaceObject2Tag', 'PlaceObject3Tag'):
                flags = 0
                if st.get('placeFlagMove') == 'true':
                    flags |= PF_MOVE
                ch = 0
                if st.get('placeFlagHasCharacter') == 'true':
                    flags |= PF_CHAR
                    ch = int(st.get('characterId'))
                m = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]
                me = st.find('matrix')
                if st.get('placeFlagHasMatrix') == 'true' and me is not None:
                    flags |= PF_MATRIX
                    if me.get('hasScale') == 'true':
                        m[0], m[3] = float(me.get('scaleX')), float(me.get('scaleY'))
                    if me.get('hasRotate') == 'true':
                        m[1], m[2] = float(me.get('rotateSkew0')), float(me.get('rotateSkew1'))
                    m[4], m[5] = int(me.get('translateX', 0)) / 20.0, int(me.get('translateY', 0)) / 20.0
                cx = [256, 256, 256, 256, 0, 0, 0, 0]
                ce = st.find('colorTransform')
                if st.get('placeFlagHasColorTransform') == 'true' and ce is not None:
                    flags |= PF_CX
                    if ce.get('hasMultTerms') == 'true':
                        cx[0:4] = [int(ce.get(k, 256)) for k in ('redMultTerm', 'greenMultTerm', 'blueMultTerm', 'alphaMultTerm')]
                    if ce.get('hasAddTerms') == 'true':
                        cx[4:8] = [int(ce.get(k, 0)) for k in ('redAddTerm', 'greenAddTerm', 'blueAddTerm', 'alphaAddTerm')]
                name = 0
                if st.get('placeFlagHasName') == 'true':
                    flags |= PF_NAME
                    name = S.add(st.get('name'))
                ratio = 0
                if st.get('placeFlagHasRatio') == 'true':
                    flags |= PF_RATIO
                    ratio = int(st.get('ratio'))
                clip = 0
                if st.get('placeFlagHasClipDepth') == 'true':
                    flags |= PF_CLIP
                    clip = int(st.get('clipDepth')) - 16384
                pidx = 0
                if st.get('placeFlagHasClipActions') == 'true':
                    fdir = os.path.join(sdir, 'frame_%d' % f) if sdir else None
                    if fdir and os.path.isdir(fdir):
                        for pd in os.listdir(fdir):
                            mm = re.match(r'^PlaceObject[23]_(\d+)_(.*)_(\d+)$', pd)
                            if mm and int(mm.group(3)) == int(st.get('depth')):
                                for fn in os.listdir(os.path.join(fdir, pd)):
                                    if 'construct' in fn:
                                        p = parse_construct(read(os.path.join(fdir, pd, fn)))
                                        font = 1 if mm.group(2) == 'DangleFont' else 0
                                        align = 1 if p.get('centered') == 'true' or p.get('align') == 'center' else (2 if p.get('align') == 'right' else 0)
                                        params.append((S.add(p.get('text', '')), int(float(p.get('tracking', 0))),
                                                       int(float(p.get('line_spacing', 9 if font == 0 else 13))), align, font))
                                        pidx = len(params)
                frame_ops[f].append(pack_op(OP_PLACE, flags, int(st.get('depth')), ch, ratio,
                                                *m, *cx, name, clip, pidx, 0))
            elif ty == 'RemoveObject2Tag':
                frame_ops[f].append(pack_op(OP_REMOVE, 0, int(st.get('depth')), 0, 0,
                                                1, 0, 0, 1, 0, 0, 256, 256, 256, 256, 0, 0, 0, 0, 0, 0, 0, 0))
            elif ty == 'StartSoundTag':
                info = st.find('soundInfo')
                fl = 0
                if info is not None and info.get('syncStop') == 'true':
                    fl |= 1
                if info is not None and info.get('syncNoMultiple') == 'true':
                    fl |= 2
                frame_ops[f].append(pack_op(OP_SOUND, fl, 0, int(st.get('soundId')), 0,
                                                1, 0, 0, 1, 0, 0, 256, 256, 256, 256, 0, 0, 0, 0, 0, 0, 0, 0))
        # acciones de frame (después de la lista de visualización)
        if sdir:
            for fr in range(1, nframes + 1):
                p = os.path.join(sdir, 'frame_%d' % fr, 'DoAction.as')
                if os.path.exists(p):
                    acts, raw = parse_actions(read(p), labels)
                    for r in raw:
                        raw_calls.setdefault(r, []).append(sid)
                    for (code, arg, s) in acts:
                        frame_ops[fr].append(pack_op(OP_ACTION, 0, 0, code, 0,
                                                         1, 0, 0, 1, 0, 0, 256, 256, 256, 256, 0, 0, 0, 0,
                                                         S.add(s) if s else 0, 0, 0, arg))
        first = len(ops)
        starts = []
        for fr in range(1, nframes + 1):
            starts.append(len(ops) - first)
            ops.extend(frame_ops[fr])
        starts.append(len(ops) - first)
        return first, starts, labels

    sprite_data = {}
    for cid, t in chars.items():
        ty = t.get('type')
        if ty == 'DefineSpriteTag':
            n = int(t.get('frameCount'))
            sprite_data[cid] = (n,) + build_timeline(t.find('subTags'), n, sprite_dirs.get(cid), cid)
        elif ty == 'DefineButton2Tag':
            # Botón: estado "up" como sprite de un frame
            fake = []
            recs = t.find('characters')
            for rec in (recs if recs is not None else []):
                if rec.get('buttonStateUp') == 'true':
                    e = ET.Element('item', {'type': 'PlaceObject2Tag', 'placeFlagHasCharacter': 'true',
                                            'characterId': rec.get('characterId'),
                                            'depth': rec.get('placeDepth'), 'placeFlagHasMatrix': 'true'})
                    if rec.find('placeMatrix') is not None:
                        pm = rec.find('placeMatrix')
                        pm.tag = 'matrix'
                        e.append(pm)
                    fake.append(e)
            sprite_data[cid] = (1,) + build_timeline(fake, 1, None, cid)
    root_frames = int(root.get('frameCount'))
    sprite_data[0] = (root_frames,) + build_timeline(tags, root_frames, scripts_dir, 0)

    # --- sonidos
    snd_blob = bytearray()
    sounds = {}
    for fn in os.listdir(os.path.join(ext, 'sounds')):
        m = re.match(r'^(\d+)', fn)
        if not m:
            continue
        sid = int(m.group(1))
        with open(os.path.join(ext, 'sounds', fn), 'rb') as fh:
            data = fh.read()
        # WAV PCM: buscar fmt y data
        pos = 12
        rate, ch, bits, pcm = 11025, 1, 16, b''
        while pos + 8 <= len(data):
            cidt, sz = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
            body = data[pos + 8:pos + 8 + sz]
            if cidt == b'fmt ':
                _, ch, rate, _, _, bits = struct.unpack('<HHIIHH', body[:16])
            elif cidt == b'data':
                pcm = body
            pos += 8 + sz + (sz & 1)
        arr = np.frombuffer(pcm, '<i2')
        if ch == 2:
            arr = arr.reshape(-1, 2).mean(axis=1).astype('<i2')
        sounds[sid] = (rate, len(arr), len(snd_blob))
        snd_blob += arr.tobytes()

    # --- niveles
    lvl_src = read(os.path.join(scripts_dir, '__Packages', 'levels', 'EmbeddedLevels.as'))
    lv = {}
    for m in re.finditer(r'\.level_(\d+) = "((?:[^"\\]|\\.)*)";', lvl_src):
        lv[int(m.group(1))] = m.group(2).replace('\\"', '"')
    link_by_name = {v: k for k, v in exports.items()}
    lvl_blob = bytearray()
    lvl_offs = []
    tile_ids_used = set()
    missing_tiles = set()
    for i in range(1, 34):
        x = ET.fromstring(lv[i])
        w, h = int(x.get('width')), int(x.get('height'))
        fg = [[0] * w for _ in range(h)]
        bg = [[0] * w for _ in range(h)]
        rows = bgrows = 0
        objs = []
        for n in x:
            if n.tag in ('row', 'bgRow'):
                cells = []
                for c in (n.text or '').split(','):
                    if ':' in c:
                        nm, k = c.split(':')
                        cells += [nm] * int(k)
                    else:
                        cells.append(c)
                grid, yy = (fg, rows) if n.tag == 'row' else (bg, bgrows)
                for xx in range(w):
                    nm = cells[xx] if xx < len(cells) else ''
                    if nm and nm != '-':
                        if nm not in link_by_name:
                            missing_tiles.add(nm)
                        # 0xFFFF: marcador "antichest"; 0xFFFE: tile sólido sin gráfico (linkage inexistente)
                        grid[yy][xx] = 0xFFFF if nm == 'antichest' else link_by_name.get(nm, 0xFFFE)
                if n.tag == 'row':
                    rows += 1
                else:
                    bgrows += 1
            elif n.tag == 'obj':
                # for..in de AS2 recorre los atributos al revés del orden escrito
                attrs = list(n.attrib.items())[::-1]
                objs.append(attrs)
        lvl_offs.append(len(lvl_blob))
        lvl_blob += struct.pack('<HHBBH', w, h, int(x.get('players', 1)), 0, len(objs))
        for row in fg:
            tile_ids_used.update(row)
            lvl_blob += struct.pack('<%dH' % w, *row)
        for row in bg:
            lvl_blob += struct.pack('<%dH' % w, *row)
        for attrs in objs:
            lvl_blob += struct.pack('<H', len(attrs))
            for k, v in attrs:
                lvl_blob += struct.pack('<II', S.add(k), S.add(v))
    if missing_tiles:
        print('[pak] tiles sin linkage (se conservan como marcador o sólido invisible):', sorted(missing_tiles))

    # --- tabla de caracteres
    maxid = max(list(chars.keys()) + list(ui_ids.values()) + [0]) + 1
    char_tab = [(K_NONE, 0, 0)] * maxid
    cdat = bytearray()

    def cd_align():
        while len(cdat) % 4:
            cdat.append(0)

    for cid, (bounds, qs) in char_quads.items():
        first, n = emit_quads(qs)
        cd_align()
        off = len(cdat)
        cdat += struct.pack('<ffffIHH', *bounds, first, n, 0)
        char_tab[cid] = (K_SHAPE, 1, off)
    for cid, ents in morph_entries.items():
        cd_align()
        off = len(cdat)
        body = bytearray()
        for (r, bb, qs) in ents:
            first, n = emit_quads(qs)
            body += struct.pack('<HHffffIHH', r, 0, *bb, first, n, 0)
        cdat += body
        char_tab[cid] = (K_MORPH, len(ents), off)
    # Sprites: cabecera en SPRT {u16 nframes, u16 nlabels, u32 primer op, u32 etiquetas, u32 frames}
    # con etiquetas {u32 nombre, u16 frame, u16 0} e inicios de frame (nframes + 1 u32) en CDAT.
    sprt = bytearray()
    sprite_index = {}
    for cid, (n, first, starts, labels) in sorted(sprite_data.items()):
        if cid >= maxid:
            continue
        cd_align()
        lab_off = len(cdat)
        for nm, fr in sorted(labels.items(), key=lambda kv: kv[1]):
            cdat += struct.pack('<IHH', S.add(nm), fr, 0)
        fr_off = len(cdat)
        cdat += struct.pack('<%dI' % len(starts), *starts)
        sprite_index[cid] = len(sprt)
        sprt += struct.pack('<HHIII', n, len(labels), first, lab_off, fr_off)
        char_tab[cid] = (K_SPRITE, len(labels), sprite_index[cid])
    for sid, (rate, ns, doff) in sounds.items():
        if sid < 0 or sid >= maxid:
            continue
        cd_align()
        off = len(cdat)
        cdat += struct.pack('<III', rate, ns, doff)
        char_tab[sid] = (K_SOUND, 1, off)

    links = sorted(((nm, cid) for cid, nm in exports.items() if cid < maxid), key=lambda kv: kv[0])

    sections = []

    def sect(sid, data):
        sections.append((sid, bytes(data)))

    # Páginas comprimidas una a una (se descomprimen al usarlas). Formato 0: RGBA8 por filas;
    # formato 1 (--3ds): ABGR8 en baldosas Morton de 8x8, listo para la GPU de la 3DS.
    fmt = 1 if TARGET_3DS else 0
    blobs = []
    for pg in packer.pages:
        raw = tile_3ds(pg) if fmt == 1 else pg.tobytes()
        blobs.append((zlib.compress(raw, 9), len(raw)))
    page_hdr = bytearray(struct.pack('<I', len(packer.pages)))
    off = 4 + 20 * len(packer.pages)
    for (cb, rawlen) in blobs:
        page_hdr += struct.pack('<HHIIII', PAGE, PAGE, off, len(cb), rawlen, fmt)
        off += len(cb)
    sect(b'PAGZ', page_hdr + b''.join(cb for cb, _ in blobs))
    qd = bytearray()
    for (pi, u, v, w, h, dx, dy) in quad_table:
        qd += struct.pack('<HHHHHhhH', pi, u, v, w, h, int(dx), int(dy), 0)
    sect(b'QUAD', qd)
    ct = bytearray(struct.pack('<I', maxid))
    for (k, n, o) in char_tab:
        ct += struct.pack('<BBHI', k, 0, n, o)
    sect(b'CHAR', ct)
    sect(b'CDAT', cdat)
    sect(b'SPRT', sprt)
    sect(b'OPS ', b''.join(ops))
    pr = bytearray(struct.pack('<I', len(params)))
    for (txt, trk, ls, al, fo) in params:
        pr += struct.pack('<IhhBBH', txt, trk, ls, al, fo, 0)
    sect(b'PRMS', pr)
    lk = bytearray(struct.pack('<I', len(links)))
    for nm, cid in links:
        lk += struct.pack('<IHH', S.add(nm), cid, 0)
    sect(b'LINK', lk)
    sect(b'SND ', snd_blob)
    lvh = bytearray(struct.pack('<I', len(lvl_offs)))
    for o in lvl_offs:
        lvh += struct.pack('<I', 4 + 4 * len(lvl_offs) + o)
    sect(b'LVL ', lvh + lvl_blob)
    # Color medio de cada tile usado en los niveles (minimapa)
    def char_avg(cid, depth=0):
        acc = np.zeros(4)
        if depth > 4:
            return acc
        if cid in char_quads:
            for (key, sx, sy, w, h, dx, dy) in char_quads[cid][1]:
                sub = images[key][sy:sy + h, sx:sx + w].reshape(-1, 4).astype(float)
                m = sub[:, 3] >= 128
                acc[:3] += sub[m, :3].sum(axis=0)
                acc[3] += m.sum()
        elif cid in chars and chars[cid].get('type') == 'DefineSpriteTag':
            for st in chars[cid].find('subTags'):
                if st.get('type') == 'ShowFrameTag':
                    break
                if st.get('type') in ('PlaceObject2Tag', 'PlaceObject3Tag') and st.get('characterId'):
                    acc += char_avg(int(st.get('characterId')), depth + 1)
        return acc
    used_tiles = set()
    for i in range(len(lvl_offs)):
        pass
    tcol = bytearray()
    tile_ids = sorted(set(v for v in tile_ids_used if 0 < v < 0xFFFE))
    tcol += struct.pack('<I', len(tile_ids))
    for cid in tile_ids:
        a = char_avg(cid)
        c = (0x80, 0x90, 0xA0) if a[3] < 1 else tuple(int(v) for v in a[:3] / a[3])
        tcol += struct.pack('<HHI', cid, 0, 0xFF000000 | (c[0] << 16) | (c[1] << 8) | c[2])
    sect(b'TCOL', tcol)
    sect(b'STR ', S.blob)

    hdr = bytearray(b'MUTP' + struct.pack('<II', 1, len(sections)))
    body = bytearray()
    base = len(hdr) + 20 * len(sections)
    for sid, data in sections:
        if sid == b'PAGZ':
            hdr += sid + struct.pack('<IIII', base + len(body), len(data), len(data), 0)
            body += data
            continue
        comp = zlib.compress(data, 9)
        hdr += sid + struct.pack('<IIII', base + len(body), len(comp), len(data), 1)
        body += comp
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    with open(out, 'wb') as fh:
        fh.write(hdr + body)

    print('[pak] formas: %(bitmap_shapes)d bitmap, %(vector_shapes)d vector, %(raster_shapes)d raster, %(empty_shapes)d vacías' % report)
    print('[pak] imágenes únicas %d en %d páginas de %d, %d quads' % (len(uniq), len(packer.pages), PAGE, len(quad_table)))
    print('[pak] sprites %d, ops %d, parámetros %d, linkage %d, sonidos %d, niveles %d' % (
        len(sprite_index), len(ops), len(params), len(links), len(sounds), len(lvl_offs)))
    print('[pak] %s: %.1f MB' % (out, os.path.getsize(out) / 1e6))
    calls = sorted(raw_calls.items(), key=lambda kv: -len(kv[1]))
    with open(os.path.join(os.path.dirname(out), 'pak_calls.txt'), 'w') as fh:
        for st, ids in calls:
            fh.write('%4d  %s   [%s]\n' % (len(ids), st, ','.join(str(i) for i in sorted(set(ids))[:12])))
    if not TARGET_3DS:
        for i, pg in enumerate(packer.pages):
            Image.fromarray(pg).save(os.path.join(os.path.dirname(out), 'page_%d.png' % i))


if __name__ == '__main__':
    main()
