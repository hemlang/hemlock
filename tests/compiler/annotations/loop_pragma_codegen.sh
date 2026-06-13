#!/usr/bin/env bash
#
# Codegen-inspection test for loop & branch annotations.
#
# The parity suite already proves the annotations don't change observable
# behavior. This test verifies the *other half* of the contract: that the
# compiler actually lowers each annotation to the matching GCC pragma /
# __builtin_expect in the generated C. Run from the repository root.

set -u

REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO_ROOT" || exit 1

HEMLOCKC="$REPO_ROOT/hemlockc"
if [ ! -x "$HEMLOCKC" ]; then
    echo "hemlockc not built; run 'make compiler' first" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SRC="$TMP_DIR/loop_pragmas.hml"
C_OUT="$TMP_DIR/loop_pragmas.c"

cat > "$SRC" <<'HML'
let total = 0;

@unroll(8)
for (let i = 0; i < 100; i++) {
    total = total + i;
}

@nounroll
for (let i = 0; i < 100; i++) {
    total = total + i;
}

@simd
for (let i = 0; i < 100; i++) {
    total = total + i;
}

@likely
if (total > 0) {
    total = total + 1;
}

@unlikely
if (total < 0) {
    total = total - 1;
}

print(total);
HML

if ! "$HEMLOCKC" "$SRC" -c --emit-c "$C_OUT" > "$TMP_DIR/err.log" 2>&1; then
    echo "FAIL: hemlockc could not emit C"
    cat "$TMP_DIR/err.log"
    exit 1
fi

FAILURES=0
check() {
    local description="$1"
    local pattern="$2"
    if grep -qF "$pattern" "$C_OUT"; then
        echo "  ok   - $description ($pattern)"
    else
        echo "  FAIL - $description: expected '$pattern' in generated C"
        FAILURES=$((FAILURES + 1))
    fi
}

echo "Loop & branch annotation codegen:"
check "@unroll(8) emits unroll pragma"     "#pragma GCC unroll 8"
check "@nounroll emits unroll 1 pragma"    "#pragma GCC unroll 1"
check "@simd emits ivdep pragma"           "#pragma GCC ivdep"
check "@likely emits builtin_expect(.,1)"  "__builtin_expect"
check "@likely sets expected value 1"      ", 1))"
check "@unlikely sets expected value 0"    ", 0))"

if [ "$FAILURES" -eq 0 ]; then
    echo "All loop/branch annotation codegen checks passed."
    exit 0
fi
echo "$FAILURES codegen check(s) failed."
exit 1
