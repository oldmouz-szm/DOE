#!/usr/bin/env bash
set -euo pipefail

# Strict paper-like runtime limits per instance:
# - time limit: 600 seconds
# - memory limit: 4 GB (virtual memory)
TIME_LIMIT=600
MEM_LIMIT_KB=$((4 * 1024 * 1024))
MAX_ITERS=2

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BIN="$BUILD_DIR/doe_maxsat"
BENCH_DIR="$ROOT_DIR/iscas85/bench"
OBS_DIR="$ROOT_DIR/obs/iscas85"
OUT_DIR="$ROOT_DIR/results/iscas85"

mkdir -p "$BUILD_DIR" "$OBS_DIR" "$OUT_DIR"

if command -v cmake >/dev/null 2>&1; then
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" -j
else
  echo "cmake not found, fallback to g++ build"
  g++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic \
    "$ROOT_DIR/src/doe_maxsat.cpp" -o "$BIN"
fi

if [[ "${USE_RC2:-0}" == "1" ]]; then
  PYTHON_BIN="$ROOT_DIR/.venv/bin/python"
  RC2_WRAPPER="$ROOT_DIR/tools/solve_with_rc2.py"
  if [[ ! -x "$PYTHON_BIN" ]]; then
    echo "RC2 mode requested but Python env not found at $PYTHON_BIN"
    exit 1
  fi
  if [[ ! -f "$RC2_WRAPPER" ]]; then
    echo "RC2 wrapper not found at $RC2_WRAPPER"
    exit 1
  fi
  SOLVER_CMD="$PYTHON_BIN $RC2_WRAPPER"
fi

if [[ -z "${SOLVER_CMD:-}" ]]; then
  echo "Please export SOLVER_CMD, e.g.:"
  echo "  export SOLVER_CMD='/path/to/open-wbo-inc'"
  echo "Or use RC2 directly:"
  echo "  USE_RC2=1 scripts/reproduce_iscas85.sh"
  exit 1
fi

# Generate scenarios only when not provided.
# Paper references existing benchmark scenarios; if you already have exact scenarios,
# put them under obs/iscas85/<circuit>.obs and this script will reuse them.
for bench in "$BENCH_DIR"/*.bench; do
  name="$(basename "$bench" .bench)"
  obs="$OBS_DIR/$name.obs"
  if [[ ! -f "$obs" ]]; then
    "$BIN" generate \
      --bench "$bench" \
      --out "$obs" \
      --count 100 \
      --seed 1 \
      --min-output-errors 1 \
      --max-output-errors 50
  fi
done

summary="$OUT_DIR/summary.csv"
echo "circuit,scenario,solved,optimal,cost,seconds,hard,soft,vars" > "$summary"

for bench in "$BENCH_DIR"/*.bench; do
  name="$(basename "$bench" .bench)"
  obs="$OBS_DIR/$name.obs"
  out_csv="$OUT_DIR/${name}.csv"

  tmp_csv="$OUT_DIR/.tmp_${name}.csv"
  rm -f "$tmp_csv"

  (
    ulimit -Sv "$MEM_LIMIT_KB"
    timeout "$TIME_LIMIT" "$BIN" run \
      --bench "$bench" \
      --obs "$obs" \
      --solver "$SOLVER_CMD" \
      --out "$tmp_csv" \
      --max-iters "$MAX_ITERS"
  ) || true

  if [[ -f "$tmp_csv" ]]; then
    mv "$tmp_csv" "$out_csv"
    tail -n +2 "$out_csv" | sed "s/^/$name,/" >> "$summary"
  else
    echo "Warning: no output for $name (timeout or failure)."
  fi
done

echo "Done. Summary: $summary"
