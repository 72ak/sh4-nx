#!/bin/sh
# Regenerates src/recomp/gen from game/sh4.exe ("SILENT HILL 4.exe", GOG) with the xboxrecomp pipeline
# (third_party/xboxrecomp). Run from the repository root: ROOT defaults to the current directory.
set -e
ROOT=${ROOT:-$PWD}
X=$ROOT/third_party/xboxrecomp
PY=$ROOT/.venv/bin/python3
[ -x "$PY" ] || PY=$ROOT/.venv/Scripts/python
B=$ROOT/build
mkdir -p "$B"
[ -z "$TRACE" ] && [ -f "$B/trace.json" ] && TRACE="$B/trace.json"  # oracle targets (tools/oracle.py)
cd "$ROOT"
if [ "$1" != --recomp-only ]; then
"$PY" tools/realign_pe.py game/sh4.exe "$B/sh4a.exe"  # sections at file offset == RVA
"$PY" tools/pe2xbe.py "$B/sh4a.exe" "$B/sh4.xbe" --imports "$B/imports.json"
cd "$X"
"$PY" -m tools.xbe_parser "$B/sh4.xbe" --json "$B/sh4_analysis.json" --quiet
# Pass 1 finds the code; its immediates and data pointers seed the functions only data refers to.
"$PY" -m tools.disasm "$B/sh4.xbe" --text-only -o "$B/disasm" --force > "$B/disasm.log" 2>&1
cd "$ROOT" && "$PY" tools/seeds.py && cd "$X"
"$PY" -m tools.disasm "$B/sh4.xbe" --text-only -o "$B/disasm" --force --seed-functions "$B/seeds.json" > "$B/disasm.log" 2>&1
"$PY" "$ROOT/tools/fix_functions.py" "$B/sh4a.exe" "$B/disasm/functions.json"
"$PY" -m tools.func_id "$B/sh4.xbe" --functions "$B/disasm/functions.json" --strings "$B/disasm/strings.json" \
    --xrefs "$B/disasm/xrefs.json" -o "$B/func_id" > "$B/func_id.log" 2>&1
"$PY" -m tools.abi_analysis "$B/sh4.xbe" --disasm-dir "$B/disasm" --func-id-dir "$B/func_id" --output-dir "$B/abi" > "$B/abi.log" 2>&1
else cd "$X"; fi
# 0x5B56B8 / 0x5B563C: the static CRT's _setjmp3 / longjmp.
"$PY" -m tools.recomp "$B/sh4.xbe" --all --split 250 --game-name sh4 --gen-dir "$ROOT/src/recomp/gen" \
    --setjmp 0x5B56B8 --longjmp 0x5B563C ${TRACE:+--trace-functions "$TRACE"} \
    --exclude-manual "$ROOT/src/game/sh4.c" \
    --disasm-dir "$B/disasm" --func-id-dir "$B/func_id" --abi-dir "$B/abi" -o "$B/recomp" > "$B/recomp.log" 2>&1
grep -E "functions \(|unresolved call targets|manual" "$B/recomp.log"
