#!/usr/bin/env python3
"""MicVST translations.

  python3 tools/i18n.py extract    -> i18n/keys.json: every Turkish text the sources show
  python3 tools/i18n.py check      -> every language file has every key (and nothing else), the
                                      leading/trailing spaces, line breaks and quotes match
  python3 tools/i18n.py generate   -> src/i18n/Translations.cpp (after check)

Keys are the Turkish texts inside tr ("..."), u8 ("..."), TRK ("...") and the parameter names of
the built-in effects (2nd argument of addFloat / addChoice / addBool) - C string literals, also
several in a row (concatenated), with their escapes resolved. Language files: i18n/<id>.json,
{ "<Turkish>": "<translation>", ... }.
"""
import json, os, re, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
LANGS = ["en", "zh", "hi", "es", "fr", "ar", "pt", "ru", "de", "ja"]

LIT = re.compile (r'"((?:[^"\\\n]|\\.)*)"')

def unescape (s):
    out, i = [], 0
    while i < len (s):
        c = s[i]
        if c == '\\' and i + 1 < len (s):
            n = s[i + 1]
            out.append ({'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'", '0': '\0'}.get (n, n))
            i += 2
        else:
            out.append (c); i += 1
    return ''.join (out)

def strip_comments (src):
    # removes // and /* */ comments outside string literals (keeps line structure)
    out, i, n = [], 0, len (src)
    while i < n:
        c = src[i]
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == '\\' else 1
            out.append (src[i:j + 1]); i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == '\\' else 1
            out.append (src[i:j + 1]); i = j + 1
        elif src.startswith ('//', i):
            j = src.find ('\n', i); j = n if j < 0 else j
            i = j
        elif src.startswith ('/*', i):
            j = src.find ('*/', i + 2); j = n if j < 0 else j + 2
            out.append ('\n' * src[i:j].count ('\n')); i = j
        else:
            out.append (c); i += 1
    return ''.join (out)

def literal_run (src, pos):
    """Concatenated literals starting at pos (after whitespace); returns (text, end) or (None, pos)."""
    parts, i = [], pos
    while True:
        while i < len (src) and src[i] in ' \t\r\n':
            i += 1
        m = LIT.match (src, i)
        if not m:
            break
        parts.append (unescape (m.group (1))); i = m.end()
    return ('' .join (parts), i) if parts else (None, pos)

def extract():
    keys = set()
    call = re.compile (r'\b(tr|u8|TRK)\s*\(')
    addp = re.compile (r'\badd(Float|Choice|Bool)\s*\(')
    for d, _, files in os.walk (os.path.join (ROOT, 'src')):
        if os.path.basename (d) == 'i18n':
            continue
        for f in files:
            if not f.endswith (('.cpp', '.h')):
                continue
            src = strip_comments (open (os.path.join (d, f), encoding='utf-8').read())
            for m in call.finditer (src):
                text, end = literal_run (src, m.end())
                if text is None:
                    continue
                rest = src[end:].lstrip()
                if rest[:1] in (')', ',') or rest[:1] == '?' or rest[:1] == ':':
                    keys.add (text)
            for m in addp.finditer (src):
                first, end = literal_run (src, m.end())     # parameter id
                if first is None:
                    continue
                rest = src[end:].lstrip()
                if not rest.startswith (','):
                    continue
                second, end2 = literal_run (src, end + (len (src[end:]) - len (rest)) + 1)
                if second is not None:
                    keys.add (second)
            # tr (v ? "a" : "b") and similar: both literals
            for m in re.finditer (r'\btr\s*\(\s*\w+\s*\?\s*"((?:[^"\\]|\\.)*)"\s*:\s*"((?:[^"\\]|\\.)*)"\s*\)', src):
                keys.add (unescape (m.group (1))); keys.add (unescape (m.group (2)))
    keys.discard ('')
    return sorted (keys)

def edge (s):
    lead = len (s) - len (s.lstrip (' '))
    trail = len (s) - len (s.rstrip (' '))
    return lead, trail, s.count ('\n')

def check (keys, verbose=True):
    ok = True
    for lang in LANGS:
        path = os.path.join (ROOT, 'i18n', lang + '.json')
        if not os.path.exists (path):
            print ('missing file', path); ok = False; continue
        t = json.load (open (path, encoding='utf-8'))
        missing = [k for k in keys if k not in t]
        extra = [k for k in t if k not in keys]
        bad = []
        for k in keys:
            if k in t:
                v = t[k]
                if not v.strip() and k.strip():
                    bad.append ((k, 'empty'))
                elif edge (k) != edge (v):
                    bad.append ((k, 'spaces/line breaks %s vs %s' % (edge (k), edge (v))))
        if missing or extra or bad:
            ok = False
            if verbose:
                print ('%s: %d missing, %d extra, %d bad' % (lang, len (missing), len (extra), len (bad)))
                for k in missing[:20]: print ('  missing:', repr (k)[:120])
                for k in extra[:20]: print ('  extra:', repr (k)[:120])
                for k, why in bad[:40]: print ('  bad:', why, repr (k)[:100])
    return ok

def c_literal (s):
    out = []
    for ch in s:
        if ch == '\\': out.append ('\\\\')
        elif ch == '"': out.append ('\\"')
        elif ch == '\n': out.append ('\\n')
        elif ch == '\t': out.append ('\\t')
        elif ch == '?': out.append ('?')
        else: out.append (ch)
    # split long literals (MSVC limit per literal) every 2000 characters
    s = ''.join (out)
    chunks = [s[i:i + 2000] for i in range (0, len (s), 2000)] or ['']
    return ' '.join ('"%s"' % c for c in chunks)

def generate (keys):
    lines = ['// Generated by tools/i18n.py from i18n/*.json - do not edit by hand.',
             '#include "Text.h"', '#include <cstring>', '', 'namespace micvst', '{', 'namespace', '{']
    for lang in LANGS:
        t = json.load (open (os.path.join (ROOT, 'i18n', lang + '.json'), encoding='utf-8'))
        lines.append ('    const TranslationPair table_%s[] = {' % lang)
        for k in keys:
            lines.append ('        { %s, %s },' % (c_literal (k), c_literal (t[k])))
        lines.append ('        { nullptr, nullptr } };')
    lines.append ('    const char* const keys[] = {')
    for k in keys:
        lines.append ('        %s,' % c_literal (k))
    lines.append ('        nullptr };')
    lines.append ('}')
    lines.append ('')
    lines.append ('const TranslationPair* translationTable (const char* id)')
    lines.append ('{')
    for lang in LANGS:
        lines.append ('    if (std::strcmp (id, "%s") == 0) return table_%s;' % (lang, lang))
    lines.append ('    return nullptr;')
    lines.append ('}')
    lines.append ('')
    lines.append ('const char* const* translationKeys() { return keys; }')
    lines.append ('}')
    os.makedirs (os.path.join (ROOT, 'src', 'i18n'), exist_ok=True)
    open (os.path.join (ROOT, 'src', 'i18n', 'Translations.cpp'), 'w', encoding='utf-8').write ('\n'.join (lines) + '\n')

if __name__ == '__main__':
    cmd = sys.argv[1] if len (sys.argv) > 1 else 'check'
    keys = extract()
    if cmd == 'extract':
        os.makedirs (os.path.join (ROOT, 'i18n'), exist_ok=True)
        json.dump (keys, open (os.path.join (ROOT, 'i18n', 'keys.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print (len (keys), 'keys')
    elif cmd == 'check':
        sys.exit (0 if check (keys) else 1)
    elif cmd == 'generate':
        if not check (keys):
            sys.exit (1)
        generate (keys)
        print ('generated', len (keys), 'keys x', len (LANGS), 'languages')
