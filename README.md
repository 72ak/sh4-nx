# Silent Hill 4: The Room — Nintendo Switch port (work in progress)

A native Nintendo Switch port of the PC version of **Silent Hill 4: The Room** (GOG build), made the same way as
[sh2-nx](https://github.com/cutarev/sh2-nx) and [sh3-nx](https://github.com/cutarev/sh3-nx) by cutarev, whose
runtime this port starts from.

It is not an emulator. `SILENT HILL 4.exe` is statically recompiled to C ahead of time (xboxrecomp, patched by
cutarev) and the C is compiled for the Switch's ARM64 CPU. The Windows layer the game expects (Direct3D 8,
DirectSound, DirectInput, kernel32...) is cutarev's reimplementation on SDL2 and OpenGL, adapted here for SH4.

**Status: boots to the title screen and into the game on the Linux build** (headless, software OpenGL): Konami
logo, title menu, New Game, difficulty select, the intro narration and the first view of Room 302. Not yet tested
on a Switch. If it crashes there, `sh4-crash.log` and `sh4.log` (with `log=1` in `sh4.ini`) next to the NRO say
where.

## What changed from sh3-nx

- `tools/realign_pe.py`: the GOG exe is 0x200-aligned; the pipeline wants sections at file offset == RVA.
- `src/game/sh4.c`: the static CRT's two memcpy copies (0x57FE90, 0x576BC0) replaced by hand, as in sh3-nx.
  No widescreen patches yet: the game runs unmodified, 4:3.
- Bridges SH4 imports that SH3 did not: `GetVersion`, `RtlUnwind`, `MsgWaitForMultipleObjects`,
  `GetQueueStatus`, `PostThreadMessageA`, `GetDesktopWindow`, `DestroyWindow`, `UnregisterClassA`,
  `GetWindowPlacement`, `GetClientRect`, `RegSetValueExA`, `RegDeleteValueA`, `ImmAssociateContextEx`.
- Registry key `SOFTWARE\Konami\SILENT HILL 4\1.00.000` opens but holds no values (the game uses its defaults).
- `CheckSpec.dll` and `drc.dll` are loaded through `LoadLibraryA` and their functions through
  `GetProcAddress`; both are left unbridged and the game skips them (it checks for NULL).
- `src/win32/dshow.c`: the FMVs (`movie/*.pac`, MPEG-1 through DirectShow) are skipped by a filter graph that
  plays nothing. SH4 never checks the HRESULTs of its graph setup, so the objects have to exist: this graph hands
  out the pins the game looks for, accepts every connection and reports EC_COMPLETE right after Run. Real
  playback would feed the game's own renderer filter from here.
- `tools/Dockerfile.linux`, `build-linux.sh`, `run-linux.sh`: a Linux build run headless in a container (Xvfb,
  Mesa llvmpipe), with scripted keys and frame dumps, for debugging without a Switch.
- The D3DX math inside the exe has 3DNow! and SSE2 paths the lifter leaves as TODO stubs; the runtime reports a
  CPU without them, so the plain x87 paths run.

## SD card layout

```text
/switch/sh4-nx/
  sh4-nx.nro
  sh4.exe          ("SILENT HILL 4.exe" from the GOG install, renamed)
  input.ini
  sh4_caps.dat
  data/            (1.2 GB)
  demo/            (1.7 GB)
  sound/           (35 MB)
  movie/           (optional, 620 MB: not played yet)
  save/            (empty folder)
  sh4.ini          (optional: resolution=WxH, cores=1, log=1, trace=1)
```

`tools/make_sd.py <sd root> --game "A:/Games/Silent Hill 4"` lays this out. Launch from the homebrew menu in
title takeover mode (hold R while starting an installed game), not from the album.

## Building

```sh
python -m venv .venv && .venv/Scripts/pip install capstone     # Windows; .venv/bin on Linux
cp "/path/to/Silent Hill 4/SILENT HILL 4.exe" game/sh4.exe
ROOT=$PWD sh tools/regen.sh          # exe -> C (about 6 minutes)
ROOT=$PWD sh tools/build-switch.sh   # C -> build/switch/sh4-nx.nro, in devkitPro's container (docker or podman)
```

## Credits

- **cutarev**: sh2-nx and sh3-nx, the runtime and tooling this port is built from, and the xboxrecomp patches.
- **sp00nznet**: xboxrecomp, the x86 to C lifter.
- **devkitPro**, **switchbrew**, SDL2, Mesa.
