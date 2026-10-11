#!/bin/sh
# Runs build/linux/sh4 headless in the sh4nx-linux container against the PC game folder (GAME), for SECONDS
# seconds, with the runtime's trace on when TRACE=1. The game folder is mounted read-only; a scratch folder
# with sh4.exe, the ini files and symlinks to data/, demo/, sound/ stands in for the SD card folder.
# Output: build/linux-run.log (stderr/stdout), build/linux-run/ (sh4.log, sh4-crash.log, saves).
set -e
ROOT=${ROOT:-$PWD}
GAME=${GAME:-"A:/Games/Silent Hill 4"}
SECONDS_TO_RUN=${SECONDS:-60}
export MSYS_NO_PATHCONV=1
mkdir -p "$ROOT/build/linux-run"
docker run --rm -v "$ROOT:/src" -v "$GAME:/game:ro" -w /src -e TRACE="${TRACE:-}" -e KEYS="${KEYS:-}" \
    -e SECS="$SECONDS_TO_RUN" sh4nx-linux sh -c '
  R=/src/build/linux-run
  cp -f /game/"SILENT HILL 4.exe" $R/sh4.exe; cp -f /game/input.ini /game/sh4_caps.dat $R/
  for d in data demo sound movie; do ln -sfn /game/$d $R/$d; done
  mkdir -p $R/save; rm -f $R/sh4-crash.log
  [ -n "$TRACE" ] && export SH4_TRACE=1
  [ -n "$KEYS" ] && export SH4_KEYS="$KEYS" SH4_SHOW=1
  cd $R && timeout -s INT "$SECS" xvfb-run -a -s "-screen 0 1280x720x24" /src/build/linux/sh4 "$R"; echo "exit=$?"
' > "$ROOT/build/linux-run.log" 2>&1 || true
tail -40 "$ROOT/build/linux-run.log"
