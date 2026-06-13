#!/usr/bin/env bash
#
# Codegen + runtime test for the @aligned(n) memory annotation.
#
# Two halves of the contract:
#   1. The compiler lowers @aligned(n) on `let b = buffer(N)` to
#      hml_val_buffer_aligned(..., n) — and only for a positive power of two.
#   2. The runtime's hml_val_buffer_aligned actually returns over-aligned,
#      free()-compatible storage.
# Run from the repository root.

set -u

REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO_ROOT" || exit 1

HEMLOCKC="$REPO_ROOT/hemlockc"
RUNTIME_LIB="$REPO_ROOT/libhemlock_runtime.a"
if [ ! -x "$HEMLOCKC" ] || [ ! -f "$RUNTIME_LIB" ]; then
    echo "hemlockc / libhemlock_runtime.a not built; run 'make compiler' first" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
FAILURES=0

# ---- Part 1: codegen lowering ----
SRC="$TMP_DIR/aligned.hml"
C_OUT="$TMP_DIR/aligned.c"
cat > "$SRC" <<'HML'
@aligned(64)
let good = buffer(128);
good[0] = 1;

@aligned(48)
let notpow2 = buffer(16);
notpow2[0] = 1;

let plain = buffer(16);
plain[0] = 1;

print(good[0] + notpow2[0] + plain[0]);
HML

if ! "$HEMLOCKC" "$SRC" -c --emit-c "$C_OUT" > "$TMP_DIR/err.log" 2>&1; then
    echo "FAIL: hemlockc could not emit C"; cat "$TMP_DIR/err.log"; exit 1
fi

aligned_calls=$(grep -c "hml_val_buffer_aligned(.*, 64)" "$C_OUT")
plain_calls=$(grep -c "hml_val_buffer(" "$C_OUT")
echo "@aligned(n) codegen:"
if [ "$aligned_calls" -ge 1 ]; then
    echo "  ok   - @aligned(64) lowers to hml_val_buffer_aligned(..., 64)"
else
    echo "  FAIL - expected an hml_val_buffer_aligned(..., 64) call"; FAILURES=$((FAILURES+1))
fi
# @aligned(48) is not a power of two and `plain` has no annotation: both must
# use the ordinary allocator, so at least two plain hml_val_buffer( calls remain.
if [ "$plain_calls" -ge 2 ]; then
    echo "  ok   - non-power-of-two and unannotated buffers stay unaligned"
else
    echo "  FAIL - expected >=2 plain hml_val_buffer( calls, found $plain_calls"; FAILURES=$((FAILURES+1))
fi

# ---- Part 2: runtime actually over-aligns and frees cleanly ----
CHECK="$TMP_DIR/check.c"
cat > "$CHECK" <<'EOF'
#include "hemlock_runtime.h"
#include <stdint.h>
#include <stdio.h>
int main(void) {
    int aligns[] = {16, 32, 64, 128, 256};
    for (int i = 0; i < 5; i++) {
        HmlValue v = hml_val_buffer_aligned(200, aligns[i]);
        if (((uintptr_t)v.as.as_buffer->data) % aligns[i] != 0) {
            printf("FAIL align=%d\n", aligns[i]);
            return 1;
        }
        hml_free(v);  /* must be free()-compatible */
    }
    printf("RUNTIME_ALIGNED_OK\n");
    return 0;
}
EOF

ZLIB_FLAG=""
echo 'int main(){return 0;}' | gcc -x c - -lz -o /dev/null 2>/dev/null && ZLIB_FLAG="-lz"
if gcc -Iruntime/include "$CHECK" "$RUNTIME_LIB" -lm -lpthread -lffi -ldl $ZLIB_FLAG -lcrypto \
        -o "$TMP_DIR/check" > "$TMP_DIR/cc.log" 2>&1; then
    out=$(LD_LIBRARY_PATH="$REPO_ROOT:${LD_LIBRARY_PATH:-}" "$TMP_DIR/check" 2>&1)
    if [ "$out" = "RUNTIME_ALIGNED_OK" ]; then
        echo "  ok   - hml_val_buffer_aligned returns over-aligned, freeable memory"
    else
        echo "  FAIL - runtime alignment check: $out"; FAILURES=$((FAILURES+1))
    fi
else
    echo "  FAIL - could not compile the runtime alignment check"; cat "$TMP_DIR/cc.log"; FAILURES=$((FAILURES+1))
fi

if [ "$FAILURES" -eq 0 ]; then
    echo "All @aligned codegen + runtime checks passed."
    exit 0
fi
echo "$FAILURES @aligned check(s) failed."
exit 1
