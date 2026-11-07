#!/usr/bin/env python3
"""Genera src/game/lines.h con los diálogos (Team.lines) y textos de armas (hoverText) del SWF.

Uso: tools/.venv/bin/python -I tools/gen_lines.py build/extract src/game/lines.h
"""
import re
import sys


def cstr(s):
    s = s.replace("\\'", "'")
    return '"' + s.replace('\\"', '"').replace('"', '\\"') + '"'


def main():
    ext, out_path = sys.argv[1], sys.argv[2]
    pk = ext + '/scripts/__Packages/com/nitrome/'
    src = open(pk + 'throwgame/Team.as', encoding='utf-8').read()
    body = re.search(r'static var lines = \{(.*?)\};\n', src, re.S).group(1)
    teams = re.findall(r'(\w+):\[((?:"(?:[^"\\]|\\.)*",?)+)\]', body)
    out = ['// Generado por tools/gen_lines.py a partir de Team.lines y WeaponSelectButton.hoverText del SWF.',
           '#ifndef MUT_LINES_H', '#define MUT_LINES_H', '#include <string.h>', '',
           'static const struct { const char *team; const char *line[4]; } TEAM_LINES[] = {']
    for name, arr in teams:
        items = re.findall(r'"((?:[^"\\]|\\.)*)"', arr)
        out.append('    {"%s", {%s}},' % (name, ', '.join(cstr(x) for x in items)))
    out += ['};', '',
            'static inline const char *team_lines(const char *team, int n) {',
            '    for (size_t i = 0; i < sizeof TEAM_LINES / sizeof TEAM_LINES[0]; i++)',
            '        if (!strcmp(TEAM_LINES[i].team, team)) return TEAM_LINES[i].line[n & 3];',
            '    return "";', '}', '']
    src2 = open(pk + 'game/WeaponSelectButton.as', encoding='utf-8').read()
    hov = re.findall(r'hover_(\w+):\["((?:[^"\\]|\\.)*)","((?:[^"\\]|\\.)*)"\]',
                     re.search(r'static var hoverText = \{(.*?)\};\n', src2, re.S).group(1))
    out.append('static const struct { const char *weapon, *title, *desc; } HOVER_TEXT[] = {')
    for w, t, d in hov:
        out.append('    {"%s", %s, %s},' % (w, cstr(t), cstr(d)))
    out += ['};', '', '#endif', '']
    with open(out_path, 'w') as fh:
        fh.write('\n'.join(out))
    print('%d equipos, %d textos de armas' % (len(teams), len(hov)))


if __name__ == '__main__':
    main()
