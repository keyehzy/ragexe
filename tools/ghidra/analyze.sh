#!/bin/bash
# Import and auto-analyze a binary into the shared Ghidra project, then export
# the function inventory. Usage: tools/ghidra/analyze.sh [binary] [--reanalyze]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${1:-$ROOT/original/client/Ragexe.exe}"
NAME="$(basename "$BIN")"
export JAVA_HOME="${JAVA_HOME:-$(brew --prefix openjdk@21)/libexec/openjdk.jdk/Contents/Home}"
HEADLESS="$(brew --prefix ghidra)/libexec/support/analyzeHeadless"
PROJ_DIR="$ROOT/analysis/ghidra"
OUT_DIR="$ROOT/analysis/exports"
mkdir -p "$PROJ_DIR" "$OUT_DIR"

if [[ "${2:-}" == "--reanalyze" ]]; then
  MODE=(-process "$NAME")
else
  MODE=(-import "$BIN" -overwrite)
fi

"$HEADLESS" "$PROJ_DIR" ragexe "${MODE[@]}" \
  -scriptPath "$ROOT/tools/ghidra/scripts" \
  -postScript ExportInventory.java "$OUT_DIR" \
  -analysisTimeoutPerFile 7200 \
  -log "$OUT_DIR/${NAME}.analysis.log" \
  -scriptlog "$OUT_DIR/${NAME}.script.log"
