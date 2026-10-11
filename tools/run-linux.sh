#!/bin/sh
# Runs build/linux/sh4 headless in the sh4nx-linux container against the PC game folder (GAME), for SECONDS
# seconds, with the runtime's trace on when TRACE=1, scripted keys with KEYS (see dinput.c), and SHOT=n
# saving frames 0-9 and then every n-th as PPM into build/linux-run/shots. The game folder
# is mounted read-only; a scratch folder in the container with sh4.exe, the ini files and symlinks to data/,
# demo/, sound/ stands in for the SD card folder. Output: build/linux-run.log (stderr/stdout: the runtime's log),
# build/linux-run/ (sh4-crash.log, the saves and the shader cache, copied back).
set -e
ROOT=${ROOT:-$PWD}
GAME=${GAME:-"A:/Games/Silent Hill 4"}
SECONDS_TO_RUN=${SECONDS:-60}
export MSYS_NO_PATHCONV=1
mkdir -p "$ROOT/build/linux-run"
docker run --rm -v "$ROOT:/src" -v "$GAME:/game:ro" -w /src -e TRACE="${TRACE:-}" -e KEYS="${KEYS:-}" \
    -e SECS="$SECONDS_TO_RUN" -e SHOT="${SHOT:-}" sh4nx-linux sh -c '
  R=/tmp/sh4; O=/src/build/linux-run; mkdir -p $R/save
  cp -f /game/"SILENT HILL 4.exe" $R/sh4.exe; cp -f /game/input.ini /game/sh4_caps.dat $R/
  for d in data demo sound movie; do ln -sfn /game/$d $R/$d; done
  [ -f $O/sh4-progs.bin ] && cp $O/sh4-progs.bin $R/
  [ -n "$TRACE" ] && export SH4_TRACE=1
  [ -n "$KEYS" ] && export SH4_KEYS="$KEYS" SH4_SHOW=1
  [ -n "$SHOT" ] && mkdir -p $R/shots && export SH4_SHOT=$R/shots SH4_SHOT_EVERY="$SHOT"
  cd $R && timeout -s INT "$SECS" xvfb-run -a -s "-screen 0 1280x720x24" /src/build/linux/sh4 "$R"; echo "exit=$?"
  rm -f $O/sh4-crash.log; cp -f $R/sh4-crash.log $R/sh4-progs.bin $O/ 2>/dev/null; cp -rf $R/save $O/ 2>/dev/null; rm -rf $O/shots; [ -d $R/shots ] && cp -r $R/shots $O/; true
' > "$ROOT/build/linux-run.log" 2>&1 || true
tail -40 "$ROOT/build/linux-run.log"
