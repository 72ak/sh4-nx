#!/usr/bin/env python3
"""Prints functions from build/disasm/asm/text.asm by name or address, compactly, for reading the game's code.

    python tools/disfn.py sub_00413880 0x412A50 [--calls]   # --calls: only calls, compares and jumps
"""
import re, sys

names = [a for a in sys.argv[1:] if not a.startswith('--')]
calls_only = '--calls' in sys.argv
text = open('build/disasm/asm/text.asm', encoding='utf-8', errors='replace').read()
for n in names:
    if not n.startswith('sub_'):
        n = 'sub_%08X' % int(n, 16)
    m = re.search(r'^; Function: %s$(.*?)(?=^; Function: |\Z)' % re.escape(n), text, re.S | re.M)
    if not m:
        print('==', n, 'not found')
        continue
    body = m.group(1)
    hdr = re.search(r'Called by: (.*)', body)
    print('==', n, '| called by:', hdr.group(1) if hdr else '-')
    for line in body.splitlines():
        line = line.strip()
        if not line.startswith('0x'):
            if 'XREF' in line:
                print('    ' + line.strip())
            continue
        line = re.sub(r'\s{2,}', ' ', line).replace('dword ptr ', '')
        if re.search(r'\b(90 nop|8bff mov edi, edi)$', line):
            continue
        if calls_only and not re.search(r'\b(call|cmp|test|j[a-z]+|ret)\b', line):
            continue
        print('  ' + line)
