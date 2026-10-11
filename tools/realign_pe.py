#!/usr/bin/env python3
"""Rewrites a PE so every section's raw data sits at file offset == RVA (file alignment 0x1000): what
tools/pe2xbe.py, seeds.py and fix_functions.py assume. "SILENT HILL 4.exe" is 0x200-aligned.

Usage: realign_pe.py game/sh4.exe build/sh4a.exe
"""
import struct, sys

d = bytearray(open(sys.argv[1], 'rb').read())
pe = struct.unpack_from('<I', d, 0x3C)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
opt = pe + 24
sh = opt + optsz
secs = [struct.unpack_from('<8sIIII', d, sh + i * 40) for i in range(nsec)]
end = max(va + max(vs, rs) for _, vs, va, rs, _ in secs)
out = bytearray(end)
hdr_end = sh + nsec * 40
out[:hdr_end] = d[:hdr_end]
for i, (name, vs, va, rs, ro) in enumerate(secs):
    out[va:va + rs] = d[ro:ro + rs]
    struct.pack_into('<I', out, sh + i * 40 + 20, va)  # PointerToRawData = RVA
struct.pack_into('<I', out, opt + 36, 0x1000)  # FileAlignment
struct.pack_into('<I', out, opt + 60, 0x1000)  # SizeOfHeaders
open(sys.argv[2], 'wb').write(out)
print(f'{sys.argv[2]}: {len(out)} bytes, {nsec} sections realigned')
