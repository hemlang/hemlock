#!/usr/bin/env bash
# UndefinedBehaviorSanitizer sweep of the COMPILED backend.
#
# Builds an instrumented copy of the runtime into a throwaway dir, compiles
# every Hemlock program under the given directories (default: the parity
# and stress suites and tests/ubsan/probes) via `hemlockc --emit-c`, links it against that runtime with
# -fsanitize=undefined, runs it, and collects every UBSan report.
#
#   tests/ubsan/run_compiled_ubsan.sh                 # the default set
#   tests/ubsan/run_compiled_ubsan.sh tests/stress    # any dirs of *.hml
#
# The runtime is built WITHOUT -fwrapv: the WASM runtime build has no
# -fwrapv, so signed overflow there is real UB. Generated program code keeps
# -fwrapv, as hemlockc always passes it.
#
# A program's own exit status and output are ignored (some tests throw on
# purpose); only UBSan reports count. Exit non-zero if any were found.
# UB a program asks for itself (raw ptr misuse) is out of scope by design,
# and none of the parity tests do that.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/tests/lib/timeout.sh"  # portable `timeout` (macOS has none)
cd "$ROOT"

HEMLOCKC="$ROOT/hemlockc"
RT_DIR="$ROOT/runtime"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
TIMEOUT="${UBSAN_TEST_TIMEOUT:-60}"
[ $# -eq 0 ] && set -- tests/parity tests/stress tests/ubsan/probes

UBSAN_CFLAGS="-fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=undefined,float-cast-overflow -fno-omit-frame-pointer"

if [ ! -x "$HEMLOCKC" ]; then
  echo "FATAL: $HEMLOCKC not found — run 'make' first" >&2
  exit 2
fi

echo "--- building UBSan runtime (isolated, no -fwrapv)"
LWS_CFLAGS="$(pkg-config --cflags libwebsockets 2>/dev/null || true)"
RT_CFLAGS="-Wall -std=c11 -O1 -g -fPIC -Iinclude -I../src/shared -I../include \
-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DHML_HAVE_ZLIB \
-DHML_HAVE_LIBWEBSOCKETS $LWS_CFLAGS $UBSAN_CFLAGS"
RT_BUILD="$WORK/rt-build"
mkdir -p "$RT_BUILD"
( cd "$RT_DIR" && make static BUILD_DIR="$RT_BUILD" CFLAGS="$RT_CFLAGS" \
  >"$WORK/rt-build.log" 2>&1 )
RT_LIB="$RT_BUILD/libhemlock_runtime.a"
if [ ! -f "$RT_LIB" ]; then
  echo "FATAL: UBSan runtime build failed" >&2
  tail -20 "$WORK/rt-build.log" >&2
  exit 2
fi

LIBS="-lm -lpthread -lffi -ldl -lz -lwebsockets -lssl -lcrypto"
ran=0; skipped=0; bad=0
REPORTS="$WORK/reports.txt"
: > "$REPORTS"

while IFS= read -r src; do
  name="$(echo "${src#./}" | tr '/' '_' | sed 's/\.hml$//')"
  cfile="$WORK/$name.c"; bin="$WORK/$name"; out="$WORK/$name.out"
  # Programs the compiler rejects are the parity suite's business, not ours
  if ! timeout 60 "$HEMLOCKC" --emit-c "$cfile" "$src" >/dev/null 2>&1 ||
     ! gcc -O1 -g -fwrapv $UBSAN_CFLAGS -rdynamic -o "$bin" "$cfile" \
         -I"$RT_DIR/include" "$RT_LIB" $LIBS 2>"$WORK/$name.cc.log"; then
    skipped=$((skipped+1)); continue
  fi
  ( cd "$(dirname "$src")" &&
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
      timeout "$TIMEOUT" "$bin" </dev/null ) >"$out" 2>&1
  ran=$((ran+1))
  if grep -q "runtime error:" "$out"; then
    bad=$((bad+1))
    echo "UB    $src"
    grep -m1 "runtime error:" "$out" | sed 's/^/      /'
    grep -m1 "runtime error:" "$out" >> "$REPORTS"
  fi
done < <(find "$@" -name '*.hml' | sort)

echo "=== compiled UBSan: $ran run, $skipped not compiled, $bad with UB ==="
if [ $bad -gt 0 ]; then
  echo "--- distinct sites"
  sed -E "s|^$ROOT/||; s/:([0-9]+):[0-9]+: runtime error/:\\1: runtime error/" "$REPORTS" |
    sort | uniq -c | sort -rn
fi
[ $bad -eq 0 ]
