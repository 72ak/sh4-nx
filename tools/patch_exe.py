#!/usr/bin/env python3
"""Byte patches applied to sh4.exe (after tools/realign_pe.py) before recompilation: the parts of ThirteenAG's
SilentHill4.WidescreenFix that change a constant in the middle of a function. Everything else in the fix is a
hand-written function in src/game/sh4.c, chosen by the call's return address, so it needs no patch here.

Each patched instruction is pointed at a variable at VARS (sh4.c sets them from the screen's aspect ratio, to
the game's own values at 4:3) or turned into a call to CAVE, a byte of int3 padding made a `ret`, which sh4.c
implements (sub_005A9379). Every site is checked against the original bytes. The original exe still goes on the
SD card: the recompiled code comes from this patched one, and the game never reads its own code bytes.

Usage: patch_exe.py build/sh4a.exe build/sh4_ws.exe
"""
import struct, sys

BASE = 0x400000
VARS = 0x0EC00000  # sh4.c: V_MENU_SCALE, V_MAP_ZOOM, V_FILM_WIDTH, V_BLUR_640
CAVE = 0x5A9379    # int3 padding after GOG's FOV code (0x5A9360), which ends in a jmp


def p32(v):
    return struct.pack('<I', v & 0xFFFFFFFF)


def call(at, target):
    return b'\xE8' + struct.pack('<i', target - (at + 5))


# (va, original bytes, replacement, what)
PATCHES = [
    (0x404DAE, p32(0x5B72B4), p32(VARS + 0x0), 'menu scale: fmul by 1/256'),
    (0x51D65D, p32(0x5B79E0), p32(VARS + 0x4), 'map zoom: fmul by 0.5'),
    (0x5681B1, p32(0x5B7DC8), p32(VARS + 0x8), 'film effect width: fmul by 0.1'),
    (0x568225, p32(0x5B7DC8), p32(VARS + 0x8), 'film effect width'),
    (0x5682C5, p32(0x5B7DC8), p32(VARS + 0x8), 'film effect width'),
    (0x568339, p32(0x5B7DC8), p32(VARS + 0x8), 'film effect width'),
    (0x566BA9, b'\xB8' + p32(640), b'\xA1' + p32(VARS + 0xC), 'blur: mov eax, 640 -> mov eax, [var]'),
    (CAVE, b'\xCC', b'\xC3', 'cave: the widescreen helper sh4.c defines'),
    (0x404DED, bytes.fromhex('c7442424cdcc4c3b'), call(0x404DED, CAVE) + b'\x90' * 3, 'text width: mov [esp+24], 0.003125'),
    (0x404E17, bytes.fromhex('c7442434000080bf'), call(0x404E17, CAVE) + b'\x90' * 3, 'text left edge: mov [esp+34], -1.0'),
]


def main():
    d = bytearray(open(sys.argv[1], 'rb').read())
    for va, old, new, what in PATCHES:
        o = va - BASE  # file offsets equal RVAs after realign_pe.py
        if d[o:o + len(old)] != old:
            sys.exit(f'{va:#x} ({what}): expected {old.hex()}, found {d[o:o + len(old)].hex()}')
        d[o:o + len(new)] = new
    open(sys.argv[2], 'wb').write(d)
    print(f'{len(PATCHES)} patches -> {sys.argv[2]}')


if __name__ == '__main__':
    main()
