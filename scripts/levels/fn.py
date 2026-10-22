#!/usr/bin/env python3
"""Print a function from split/asm/1050.s compactly: fn.py func_XXXXXXXX [...]; fn.py --callers func_X; fn.py --at 0x8006XXXX"""
import re, sys
L = open('split/asm/1050.s').read().split('\n')
starts = {}
for i, l in enumerate(L):
    m = re.match(r'glabel (\S+)', l)
    if m:
        starts[m.group(1)] = i
order = sorted(starts.items(), key=lambda x: x[1])

def body(name):
    i = starts[name]; out = []
    for l in L[i:]:
        m = re.match(r'\s*/\* \w+ (\w+) \w+ \*/\s*(.*)', l)
        if m: out.append(f'{m.group(1)} {m.group(2)}')
        elif l.strip() and not l.startswith('nonmatching'): out.append(l.strip())
        if l.startswith('endlabel'): break
    return out

def owner(lineno):
    best = None
    for n, i in order:
        if i <= lineno: best = n
        else: break
    return best

a = sys.argv[1:]
if a[0] == '--callers':
    for f in a[1:]:
        seen = []
        for i, l in enumerate(L):
            if re.search(r'jal\s+' + f + r'\b', l):
                m = re.match(r'\s*/\* \w+ (\w+)', l)
                seen.append(f'{owner(i)}@{m.group(1)}')
        print(f, ' '.join(seen))
elif a[0] == '--at':
    v = a[1].upper().replace('0X', '')
    for i, l in enumerate(L):
        if re.match(r'\s*/\* \w+ ' + v + ' ', l):
            print(owner(i)); break
else:
    for f in a:
        print('\n'.join(body(f))); print()
