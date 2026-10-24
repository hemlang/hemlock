#!/usr/bin/env bash
# UndefinedBehaviorSanitizer sweep of the INTERPRETER (and of hemlockc
# itself, which the parity suite runs).
#
# Rebuilds hemlock + hemlockc in-tree with -fsanitize=undefined and
# -fno-wrapv (the WASM builds have no -fwrapv, so signed overflow there is
# real UB), then runs the interpreter suite, the parity suite and every
# tests/ubsan/probes/*.hml, logging each UBSan report. Exit non-zero if any
# report was logged. Leaves instrumented binaries behind: run `make clean &&
# make` afterwards for normal ones.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/tests/lib/timeout.sh"
cd "$ROOT"
LOGS="$(mktemp -d)"
trap 'rm -rf "$LOGS"' EXIT

# tests/run_tests.sh does its own `make clean && make`; make reads these
# from the environment, so the rebuild stays instrumented.
export EXTRA_CFLAGS="-fsanitize=undefined,float-cast-overflow -fno-wrapv -fno-omit-frame-pointer -g -O1"
export EXTRA_LDFLAGS="-fsanitize=undefined"
export UBSAN_OPTIONS="log_path=$LOGS/ubsan:print_stacktrace=1:report_error_type=1"

echo "--- building instrumented hemlock + hemlockc"
make clean >/dev/null 2>&1
if ! make -j"$(nproc 2>/dev/null || echo 2)" >"$LOGS/build.log" 2>&1; then
  echo "FATAL: instrumented build failed" >&2; tail -20 "$LOGS/build.log" >&2; exit 2
fi

echo "--- interpreter suite";  bash tests/run_tests.sh >"$LOGS/tests.log" 2>&1 || true
echo "--- parity suite";      bash tests/parity/run_parity_tests.sh >"$LOGS/parity.log" 2>&1 || true
echo "--- probes"
for f in tests/ubsan/probes/*.hml; do
  ( cd "$(dirname "$f")" && timeout 30 "$ROOT/hemlock" "$(basename "$f")" </dev/null ) >/dev/null 2>&1 || true
done

reports=$(cat "$LOGS"/ubsan.* 2>/dev/null | grep "runtime error:" || true)
if [ -z "$reports" ]; then
  echo "=== interpreter UBSan: no reports ==="
  exit 0
fi
echo "=== interpreter UBSan: reports by site ==="
echo "$reports" | sed -E "s|^$ROOT/||; s/:([0-9]+):[0-9]+: runtime error/:\\1: runtime error/" |
  sort | uniq -c | sort -rn
exit 1
