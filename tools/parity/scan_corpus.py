#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Scan mIRC script folders and count which events, commands, identifiers and
feature areas they use. Output (JSON on $OUT, summary on stdout) feeds
import_help_index.py --corpus so the parity matrix is ordered by real usage.

Usage: OUT=usage.json scan_corpus.py <dir> [<dir>...]
"""
import re, sys, os, collections, json
roots = sys.argv[1:]
exts = ('.mrc', '.mud', '.ini', '.txt')
files = []
for r in roots:
    for dp, dn, fn in os.walk(r):
        for f in fn:
            if f.lower().endswith(exts): files.append(os.path.join(dp, f))
events = collections.Counter(); raws = collections.Counter(); ctcps = collections.Counter()
cmds = collections.Counter(); idents = collections.Counter(); props = collections.Counter()
flags = collections.Counter(); nlines = 0; nfiles = 0
cmd_re = re.compile(r'(?:^|[{|]\s*|\n\s*)[/.!]*([a-zA-Z_][a-zA-Z0-9_]*)')
for p in files:
    try: s = open(p, encoding='latin-1').read()
    except Exception: continue
    # heuristics: a script file has on/alias/raw/menu or lines starting with /
    if not re.search(r'^\s*(on\s+[^:]+:|alias\s|raw\s|ctcp\s|menu\s|dialog\s|n\d+=)', s, re.M): continue
    nfiles += 1; nlines += s.count('\n')
    for m in re.finditer(r'^\s*(?:n\d+=)?on\s+[^:\n]+:([A-Za-z*]+):', s, re.M | re.I): events[m.group(1).upper()] += 1
    for m in re.finditer(r'^\s*(?:n\d+=)?raw\s+[^:\n]*?(\d+|\*):', s, re.M | re.I): raws[m.group(1)] += 1
    for m in re.finditer(r'^\s*(?:n\d+=)?ctcp\s+[^:\n]+:([A-Za-z*]+):', s, re.M | re.I): ctcps[m.group(1).upper()] += 1
    for line in s.splitlines():
        line = re.sub(r'^n\d+=', '', line.strip())
        if not line or line.startswith(';'): continue
        for stmt in re.split(r'\s\|\s|{\s*|}\s*', line):
            stmt = stmt.strip()
            m = re.match(r'[/.!]*([a-zA-Z_][a-zA-Z0-9_]*)', stmt)
            if m and (stmt.startswith('/') or stmt.startswith('.') or m.group(1).lower() not in ('on','raw','ctcp','alias','menu','dialog','if','elseif','else','while','return','halt','n')):
                w = m.group(1).lower()
                if w not in ('if','elseif','else','while','var','return','halt','haltdef','break','continue','goto','on','raw','ctcp','alias','menu','dialog'): cmds['/' + w] += 1
                else: cmds[w] += 1
    for m in re.finditer(r'\$([a-zA-Z_][a-zA-Z0-9_]*)(\([^)\n]*\))?(\.[a-zA-Z0-9_]+)?', s):
        name = '$' + m.group(1).lower(); idents[name] += 1
        if m.group(3): props[name + '()' + m.group(3).lower()] += 1
    for k, rx in {'dll': r'\$dll\b|/dll\b|\bdll\s', 'com': r'\$com\b|/com\b', 'dde': r'\$dde\b|/dde\b', 'agent': r'/gload|/gtalk|/gplay', 'run': r'/run\b|\brun\s',
                  'sockets': r'sockopen|sockread|sockwrite|socklisten|\$sock\(', 'dialog': r'^\s*dialog\s|\$dialog\(|/did\b|\bdid\s', 'window': r'/window\b|\bwindow\s|@\w', 'picture': r'drawpic|drawtext|drawrect|drawline|drawfill',
                  'hash': r'\bhadd\b|\bhget\b|\$hget\(|\bhmake\b', 'timers': r'\btimer\w*\s|\$timer\(', 'regex': r'\$regex|\$regsub|\$regml', 'bvar': r'\bbset\b|\$bvar|\bbwrite\b|\bbread\b', 'fileio': r'\bfopen\b|\$fopen|\bwrite\b|\$read\(|writeini|\$readini|\$ini\(', 'sound': r'/splay|\bsplay\s|/sound\b|\$sound\(|\bbeep\b', 'ial': r'\$ial\(|\$ialchan', 'scon': r'/scon|/scid|\$scon|\$scid', 'filter': r'\bfilter\b|\bsavebuf\b|\bloadbuf\b', 'input': r'\$input\(|\$\?', 'dcc': r'\bdcc\s|\bfserve\b|\$send\(|\$get\(', 'mdx': r'\bmdx\b|\$mdx', 'popups': r'^\s*menu\s'}.items():
        flags[k] += len(re.findall(rx, s, re.M | re.I))
print(json.dumps({'files': nfiles, 'lines': nlines}))
def top(c, n): return ', '.join(f'{k}:{v}' for k, v in c.most_common(n))
print('EVENTS:', top(events, 60)); print('RAW:', top(raws, 40)); print('CTCP:', top(ctcps, 20))
print('CMDS:', top(cmds, 120)); print('IDENTS:', top(idents, 150)); print('PROPS:', top(props, 60)); print('FLAGS:', top(flags, 40))
json.dump({'events': events, 'raws': raws, 'ctcps': ctcps, 'cmds': cmds, 'idents': idents, 'props': props, 'flags': flags}, open(os.environ.get('OUT', '/dev/null'), 'w'))
