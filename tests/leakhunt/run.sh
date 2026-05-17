#!/usr/bin/env bash
# Per-construct LSan leak hunt.
#
# Sibling of tests/stress/run_stress.sh, but built for the codegen-leak
# AUDIT loop rather than regression gating:
#   - each tests/leakhunt/*.hml is a SINGLE-construct micro-repro, so a
#     LeakSanitizer hit isolates to one codegen path (there are no
#     #line directives in generated C, so single-construct isolation
#     is how we map a leak back to a construct).
#   - the instrumented runtime is built ONCE into a cache dir and
#     reused across runs + single-construct re-runs, so the fix loop's
#     verify step is seconds (a codegen fix rebuilds hemlockc, NOT the
#     runtime — runtime is invariant under codegen changes).
#   - the FULL LeakSanitizer report per construct is captured to
#     out/<name>.lsan for triage (run_stress only greps a summary).
#
# Usage:
#   ./run.sh                 # all micro-repros
#   ./run.sh closure_capture # just one (fix-loop verify; name w/o .hml)
#   ./run.sh --rebuild-rt    # force a fresh sanitizer-runtime build
#
# Exit: 0 iff every run was PASS (no leak, no crash). Non-zero lists
# LEAK/CRASH so the loop can gate on it.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HEMLOCKC="$ROOT/hemlockc"
RT_DIR="$ROOT/runtime"
LH_DIR="$ROOT/tests/leakhunt"
RT_CACHE="$LH_DIR/.rt"          # cached instrumented runtime
OUT_DIR="$LH_DIR/out"
SUPP="$ROOT/tests/stress/lsan.supp"

REBUILD_RT=0
ONLY=""
for a in "$@"; do
  case "$a" in
    --rebuild-rt) REBUILD_RT=1 ;;
    *) ONLY="$a" ;;
  esac
done

[ -x "$HEMLOCKC" ] || { echo "FATAL: $HEMLOCKC missing — run 'make compiler'"; exit 2; }
mkdir -p "$OUT_DIR"

SAN_CFLAGS="-fsanitize=address -fno-omit-frame-pointer"
# Run to completion (no halt_on_error) so the FULL leak report dumps;
# exitcode=42 distinguishes "leaked" from "crashed" (nonzero other).
SAN_ENV="ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:exitcode=42 LSAN_OPTIONS=suppressions=$SUPP"

# --- build (or reuse cached) instrumented runtime ---------------------
RT_LIB="$RT_CACHE/libhemlock_runtime.a"
if [ $REBUILD_RT -eq 1 ] || [ ! -f "$RT_LIB" ]; then
  echo "--- building instrumented runtime (cache: $RT_CACHE)"
  rm -rf "$RT_CACHE"; mkdir -p "$RT_CACHE"
  LWS_CFLAGS="$(pkg-config --cflags libwebsockets 2>/dev/null || true)"
  RT_CFLAGS="-Wall -std=c11 -O1 -g -fPIC -Iinclude -I../src/shared \
-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DHML_HAVE_ZLIB \
-DHML_HAVE_LIBWEBSOCKETS $LWS_CFLAGS $SAN_CFLAGS"
  ( cd "$RT_DIR" && make static BUILD_DIR="$RT_CACHE" CFLAGS="$RT_CFLAGS" \
    >"$RT_CACHE/build.log" 2>&1 )
  [ -f "$RT_LIB" ] || { echo "FATAL: sanitizer runtime build failed"; tail -20 "$RT_CACHE/build.log"; exit 2; }
fi

LIBS="-lm -lpthread -lffi -ldl -lz -lwebsockets -lssl -lcrypto"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

if [ -n "$ONLY" ]; then glob="$LH_DIR/$ONLY.hml"; else glob="$LH_DIR"/*_leak.hml; fi

pass=0; leak=0; crash=0; leaked_names=""
for src in $glob; do
  [ -e "$src" ] || { echo "no such micro-repro: $src"; exit 2; }
  name="$(basename "$src" .hml)"
  cfile="$WORK/$name.c"; bin="$WORK/$name"; rep="$OUT_DIR/$name.lsan"

  if ! "$HEMLOCKC" --emit-c "$cfile" "$src" >/dev/null 2>"$WORK/$name.gen"; then
    echo "CRASH $name (codegen failed)"; cat "$WORK/$name.gen"; crash=$((crash+1)); leaked_names="$leaked_names $name"; continue
  fi
  if ! gcc -O1 -g $SAN_CFLAGS -rdynamic -o "$bin" "$cfile" \
        -I"$RT_DIR/include" "$RT_LIB" $LIBS 2>"$WORK/$name.cc"; then
    echo "CRASH $name (cc failed)"; cat "$WORK/$name.cc"; crash=$((crash+1)); leaked_names="$leaked_names $name"; continue
  fi

  env $SAN_ENV timeout 120 "$bin" >"$rep" 2>&1
  rc=$?
  if [ $rc -eq 42 ] || grep -q "ERROR: LeakSanitizer" "$rep"; then
    n=$(grep -cE "Direct leak|Indirect leak" "$rep")
    echo "LEAK  $name  ($n leak blocks; report: tests/leakhunt/out/$name.lsan)"
    leak=$((leak+1)); leaked_names="$leaked_names $name"
  elif [ $rc -ne 0 ]; then
    echo "CRASH $name  (exit $rc)"; tail -3 "$rep"
    crash=$((crash+1)); leaked_names="$leaked_names $name"
  else
    echo "PASS  $name  ($(tail -1 "$rep"))"; pass=$((pass+1))
  fi
done

echo "=== leakhunt: $pass pass, $leak leak, $crash crash ==="
[ -n "$leaked_names" ] && echo "worklist:$leaked_names"
[ $((leak+crash)) -eq 0 ]
