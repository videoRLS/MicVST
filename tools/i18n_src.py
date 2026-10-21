#!/usr/bin/env python3
"""Builds i18n/<lang>.json from i18n/src/<lang>.txt (easier to write and review by hand).

Format of a .txt file: a line "@@<n>" starts the translation of key n of i18n/keys.json; the text
follows on the next lines, up to the next "@@" line (the last line break before it is not part of
the text). No escaping: quotes, backslashes and leading/trailing spaces are taken as written.

  python3 tools/i18n_src.py <lang> [<lang> ...]   (no argument: every .txt in i18n/src)
"""
import json, os, re, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))

def parse (path):
    out, cur, buf = {}, None, []
    for line in open (path, encoding='utf-8').read().split ('\n'):
        m = re.fullmatch (r'@@(\d+)', line)
        if m:
            if cur is not None:
                out[cur] = '\n'.join (buf)
            cur, buf = int (m.group (1)), []
        elif cur is not None:
            buf.append (line)
    if cur is not None:
        out[cur] = '\n'.join (buf).rstrip ('\n')
    # every block ends with one line break before the next marker
    return out

def build (lang):
    keys = json.load (open (os.path.join (ROOT, 'i18n', 'keys.json'), encoding='utf-8'))
    src = parse (os.path.join (ROOT, 'i18n', 'src', lang + '.txt'))
    bad = [n for n in src if n >= len (keys)]
    if bad:
        sys.exit ('%s: unknown key numbers %s' % (lang, bad))
    missing = [n for n in range (len (keys)) if n not in src]
    if missing:
        print ('%s: missing %s' % (lang, missing))
    def edges (k, v):
        # the spaces at both ends belong to the key (pieces joined with numbers and names):
        # taken over from the Turkish text, so editors that trim lines do no harm
        lead = k[:len (k) - len (k.lstrip (' '))]
        trail = k[len (k.rstrip (' ')):]
        return lead + v.strip (' ') + trail
    t = {keys[n]: edges (keys[n], v) for n, v in sorted (src.items())}
    json.dump (t, open (os.path.join (ROOT, 'i18n', lang + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print (lang, len (t), 'of', len (keys))

if __name__ == '__main__':
    langs = sys.argv[1:] or sorted (f[:-4] for f in os.listdir (os.path.join (ROOT, 'i18n', 'src')) if f.endswith ('.txt'))
    for l in langs:
        build (l)
