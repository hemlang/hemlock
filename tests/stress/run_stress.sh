#!/usr/bin/env bash
# Concurrency / lifetime stress harness.
#
# Compiles every tests/stress/*.hml as a NATIVE binary (the bugs this
# guards against — refcount exhaustion, concurrent object/string
# heap corruption — only exist in the compiled runtime, not the
# interpreter) and runs it under an optional sanitizer.
#
#   ./run_stress.sh            # plain build, just run + check exit
#   ./run_stress.sh asan       # AddressSanitizer (use-after-free / double-free / overflow)
#   ./run_stress.sh tsan       # ThreadSanitizer (data races)
#
# A sanitizer runtime is built into a throwaway dir so it never
# clobbers the normal ./libhemlock_runtime.a. Exit non-zero if any
# test crashes, times out, or the sanitizer reports an error.
set -u

MODE="${1:-none}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

HEMLOCKC="$ROOT/hemlockc"
RT_DIR="$ROOT/runtime"
STRESS_DIR="$ROOT/tests/stress"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

TIMEOUT="${STRESS_TIMEOUT:-180}"

# Run wrapper: TSan refuses to start under ASLR on PIE binaries
# ("FATAL: ThreadSanitizer: unexpected memory mapping"), so TSan
# binaries are linked -no-pie AND launched via `setarch -R` to
# disable per-exec address randomization. setarch may be absent
# (or fail unprivileged in some sandboxes) — fall back to bare exec.
RUN_WRAP=""

case "$MODE" in
  none)  SAN_CFLAGS="";                                     SAN_ENV="" ;;
  asan)  SAN_CFLAGS="-fsanitize=address -fno-omit-frame-pointer";
         SAN_ENV="ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1" ;;
  lsan)  SAN_CFLAGS="-fsanitize=address -fno-omit-frame-pointer";
         SAN_ENV="ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 LSAN_OPTIONS=suppressions=$STRESS_DIR/lsan.supp" ;;
  tsan)  SAN_CFLAGS="-fsanitize=thread -fno-omit-frame-pointer -no-pie -fno-pie";
         SAN_ENV="TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1"
         if command -v setarch >/dev/null 2>&1 && \
            setarch "$(uname -m)" -R true >/dev/null 2>&1; then
           RUN_WRAP="setarch $(uname -m) -R"
         fi ;;
  *) echo "usage: $0 [none|asan|lsan|tsan]" >&2; exit 2 ;;
esac

echo "=== stress harness (mode=$MODE) ==="

if [ ! -x "$HEMLOCKC" ]; then
  echo "FATAL: $HEMLOCKC not found — run 'make' first" >&2
  exit 2
fi

# Base runtime cflags mirror runtime/Makefile (Linux path) minus the
# optimization level we override for sanitizer-friendly stacks.
LWS_CFLAGS="$(pkg-config --cflags libwebsockets 2>/dev/null || true)"
RT_CFLAGS="-Wall -std=c11 -O1 -g -fPIC -Iinclude -I../src/shared \
-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DHML_HAVE_ZLIB \
-DHML_HAVE_LIBWEBSOCKETS $LWS_CFLAGS $SAN_CFLAGS"

# Build the instrumented runtime into an ISOLATED build dir. Never
# touch runtime/build — that holds the normal -fPIC objects the
# shared lib / the rest of `make` depend on; a `make clean` + custom
# CFLAGS build there leaves non-PIC objects that break the next
# `make runtime` (".so: recompile with -fPIC").
echo "--- building ${MODE} runtime (isolated)"
RT_BUILD="$WORK/rt-build"
mkdir -p "$RT_BUILD"
( cd "$RT_DIR" && make static BUILD_DIR="$RT_BUILD" CFLAGS="$RT_CFLAGS" \
  >"$WORK/rt-build.log" 2>&1 )
RT_LIB="$RT_BUILD/libhemlock_runtime.a"
if [ ! -f "$RT_LIB" ]; then
  echo "FATAL: sanitizer runtime build failed" >&2
  tail -20 "$WORK/rt-build.log" >&2
  exit 2
fi

LIBS="-lm -lpthread -lffi -ldl -lz -lwebsockets -lssl -lcrypto"
fail=0
pass=0

# lsan mode runs ONLY the dedicated *_leak.hml regression tests.
# LeakSanitizer on the broader concurrency stress tests surfaces a
# pile of *separate, pre-existing* refcount leaks (tracked for the
# ongoing codegen audit) that would make this lane permanently red
# and drown out a real regression. The leak guard's job is "the
# bugs we fixed stay fixed"; crash/race coverage of the other tests
# lives in asan/tsan.
if [ "$MODE" = "lsan" ]; then
  glob="$STRESS_DIR"/*_leak.hml
else
  glob="$STRESS_DIR"/*.hml
fi

for src in $glob; do
  name="$(basename "$src" .hml)"
  cfile="$WORK/$name.c"
  bin="$WORK/$name"

  if ! "$HEMLOCKC" --emit-c "$cfile" "$src" >/dev/null 2>"$WORK/$name.gen.log"; then
    echo "FAIL  $name  (codegen failed)"; cat "$WORK/$name.gen.log"; fail=$((fail+1)); continue
  fi
  if ! gcc -O1 -g $SAN_CFLAGS -rdynamic -o "$bin" "$cfile" \
        -I"$RT_DIR/include" "$RT_LIB" $LIBS 2>"$WORK/$name.cc.log"; then
    echo "FAIL  $name  (native compile failed)"; cat "$WORK/$name.cc.log"; fail=$((fail+1)); continue
  fi

  out="$WORK/$name.out"
  env $SAN_ENV timeout "$TIMEOUT" $RUN_WRAP "$bin" >"$out" 2>&1
  rc=$?

  if [ $rc -eq 124 ]; then
    echo "FAIL  $name  (timeout after ${TIMEOUT}s)"; tail -5 "$out"; fail=$((fail+1))
  elif [ $rc -ne 0 ]; then
    echo "FAIL  $name  (exit $rc)"
    grep -E 'Sanitizer|runtime: fatal|double free|use-after-free|SUMMARY|data race' "$out" | head -12
    fail=$((fail+1))
  elif grep -qE 'AddressSanitizer|ThreadSanitizer|runtime: fatal' "$out"; then
    echo "FAIL  $name  (sanitizer report despite exit 0)"
    grep -E 'Sanitizer|SUMMARY|data race' "$out" | head -12
    fail=$((fail+1))
  else
    echo "PASS  $name  ($(tail -1 "$out"))"
    pass=$((pass+1))
  fi
done

echo "=== stress: $pass passed, $fail failed (mode=$MODE) ==="
[ $fail -eq 0 ]
