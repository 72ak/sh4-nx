#!/usr/bin/env python3
"""Byte-pattern search over an exe's sections, in the "8B 0D ? ? ? ? 6A 00" syntax of ThirteenAG's
Hooking.Patterns, so the widescreen fix's patterns can be looked up in sh4.exe as written.

    python tools/pattern.py game/sh4.exe "8B 0D ? ? ? ? 6A 00 50 51" ...   # prints the matches' addresses
"""
import re, struct, sys


def load(path):
    """(bytes, image base, [(va, raw offset, size)]) of the exe's sections."""
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        _, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + optsz + i * 40)
        secs.append((base + va, ro, min(vs, rs)))
    return d, base, secs


def compile_pattern(p):
    return re.compile(b''.join(b'.' if t == '?' else re.escape(bytes([int(t, 16)])) for t in p.split()), re.S)


def find(img, p):
    d, base, secs = img
    rx = compile_pattern(p)
    out = []
    for va, ro, size in secs:
        blob = d[ro:ro + size]
        # overlapping matches, as a byte-by-byte scan finds them
        pos = 0
        while True:
            m = rx.search(blob, pos)
            if not m:
                break
            out.append(va + m.start())
            pos = m.start() + 1
    return out


def va_to_off(img, va):
    d, base, secs = img
    for sva, ro, size in secs:
        if sva <= va < sva + size:
            return ro + va - sva
    raise ValueError(hex(va))


def u32(img, va):
    return struct.unpack_from('<I', img[0], va_to_off(img, va))[0]


def call_dest(img, va):
    """Destination of an E8/E9 rel32 at va."""
    return (va + 5 + struct.unpack_from('<i', img[0], va_to_off(img, va) + 1)[0]) & 0xFFFFFFFF


if __name__ == '__main__':
    img = load(sys.argv[1])
    for p in sys.argv[2:]:
        print(p, '->', ' '.join(f'{a:#x}' for a in find(img, p)))
