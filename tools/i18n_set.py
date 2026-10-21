#!/usr/bin/env python3
"""Replaces single entries of i18n/src/<lang>.txt:  i18n_set.py <lang> <n> <text> [<n> <text> ...]"""
import os, re, sys
ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
lang, args = sys.argv[1], sys.argv[2:]
path = os.path.join (ROOT, 'i18n', 'src', lang + '.txt')
s = open (path, encoding='utf-8').read()
for n, text in zip (args[0::2], args[1::2]):
    pat = re.compile (r'(?m)^@@%s\n(.*?)(?=^@@\d+$|\Z)' % n, re.S)
    if not pat.search (s):
        sys.exit ('no entry %s' % n)
    s = pat.sub (lambda m: '@@%s\n%s\n' % (n, text), s, count=1)
open (path, 'w', encoding='utf-8').write (s)
