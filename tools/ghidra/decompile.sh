#!/bin/bash
# Decompile functions from the shared Ghidra project without changing it.
# Usage: tools/ghidra/decompile.sh OUTFILE ADDRESS [ADDRESS ...]   (hex, without 0x)
# Batch the addresses: each run starts Ghidra once, and most of its time is start-up.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
shift
export JAVA_HOME="${JAVA_HOME:-$(brew --prefix openjdk@21)/libexec/openjdk.jdk/Contents/Home}"
HEADLESS="$(brew --prefix ghidra)/libexec/support/analyzeHeadless"
"$HEADLESS" "$ROOT/analysis/ghidra" ragexe -process Ragexe.exe -noanalysis -readOnly \
  -scriptPath "$ROOT/tools/ghidra/scripts" -postScript Decompile.java "$OUT" "$@" > "$OUT.log" 2>&1 \
  || { echo "decompile failed; see $OUT.log" >&2; exit 1; }
rm -f "$OUT.log"
