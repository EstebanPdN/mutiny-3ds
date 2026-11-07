#!/usr/bin/env python3
"""Muestra la línea de tiempo de un símbolo del SWF (por id o nombre de linkage).

Uso: tools/.venv/bin/python -I tools/swfinfo.py build/extract/swf.xml <id|linkage> [profundidad]
"""
import sys
import xml.etree.ElementTree as ET


def load(path):
    root = ET.parse(path).getroot()
    tags = root.find('tags')
    chars, exports = {}, {}
    for t in tags:
        ty = t.get('type')
        cid = (t.get('shapeId') or t.get('spriteId') or t.get('characterID') or t.get('buttonId')
               or t.get('fontID') or t.get('fontId') or t.get('soundId') or t.get('characterId'))
        if ty.startswith('Define') and cid:
            chars[int(cid)] = t
        if ty == 'ExportAssetsTag':
            for a, b in zip(t.find('tags'), t.find('names')):
                exports[int(a.text)] = b.text
    return tags, chars, exports


def mat(m):
    if m is None:
        return ''
    s = 'tx=%.1f ty=%.1f' % (int(m.get('translateX', 0)) / 20, int(m.get('translateY', 0)) / 20)
    if m.get('hasScale') == 'true':
        s += ' sx=%.3f sy=%.3f' % (float(m.get('scaleX')), float(m.get('scaleY')))
    if m.get('hasRotate') == 'true':
        s += ' r0=%.3f r1=%.3f' % (float(m.get('rotateSkew0')), float(m.get('rotateSkew1')))
    return s


def dump(tags, chars, exports, cid, depth, indent=''):
    t = chars.get(cid)
    if t is None:
        print(indent + '?', cid)
        return
    ty = t.get('type')
    print('%s[%d] %s %s' % (indent, cid, ty, exports.get(cid, '')))
    if ty != 'DefineSpriteTag' or depth <= 0:
        return
    frame = 1
    seen = set()
    for st in t.find('subTags'):
        sty = st.get('type')
        if sty == 'ShowFrameTag':
            frame += 1
        elif sty == 'FrameLabelTag':
            print('%s  f%d label "%s"' % (indent, frame, st.get('name')))
        elif sty in ('PlaceObject2Tag', 'PlaceObject3Tag'):
            ch = st.get('characterId')
            print('%s  f%d place d=%s ch=%s name=%s %s%s%s' % (
                indent, frame, st.get('depth'), ch, st.get('name'),
                mat(st.find('matrix')), ' cx' if st.find('colorTransform') is not None else '',
                (' ratio=' + st.get('ratio')) if st.get('placeFlagHasRatio') == 'true' else ''))
            if ch and int(ch) not in seen:
                seen.add(int(ch))
                dump(tags, chars, exports, int(ch), depth - 1, indent + '    ')
        elif sty == 'RemoveObject2Tag':
            print('%s  f%d remove d=%s' % (indent, frame, st.get('depth')))
        elif sty == 'DoActionTag':
            print('%s  f%d <action>' % (indent, frame))
        elif sty == 'StartSoundTag':
            print('%s  f%d sound %s' % (indent, frame, st.get('soundId')))
    print('%s  (%s frames)' % (indent, t.get('frameCount')))


if __name__ == '__main__':
    tags, chars, exports = load(sys.argv[1])
    key = sys.argv[2]
    depth = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    if key.isdigit():
        cid = int(key)
    else:
        cid = next(k for k, v in exports.items() if v == key)
    dump(tags, chars, exports, cid, depth)
