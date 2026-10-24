#!/bin/bash
# Sandbox tests: each tests/sandbox/*.hml runs under --sandbox on both
# backends, and both outputs must match the .expected file.
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HEMLOCK="$ROOT_DIR/hemlock"
HEMLOCKC="$ROOT_DIR/hemlockc"
TEST_DIR="$ROOT_DIR/tests/sandbox"
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

failed=0
passed=0

for test_file in "$TEST_DIR"/*.hml; do
    name=$(basename "$test_file" .hml)
    expected="$TEST_DIR/$name.expected"

    timeout 30 "$HEMLOCK" --sandbox "$test_file" > "$TEMP_DIR/$name.interp" 2>&1
    if diff -u "$expected" "$TEMP_DIR/$name.interp" > "$TEMP_DIR/$name.interp.diff"; then
        passed=$((passed + 1))
    else
        echo "✗ $name (interpreter)"
        cat "$TEMP_DIR/$name.interp.diff"
        failed=$((failed + 1))
    fi

    if ! timeout 300 "$HEMLOCKC" --sandbox "$test_file" -o "$TEMP_DIR/$name.bin" > "$TEMP_DIR/$name.cc" 2>&1; then
        echo "✗ $name (compiler: build failed)"
        cat "$TEMP_DIR/$name.cc"
        failed=$((failed + 1))
        continue
    fi
    timeout 30 "$TEMP_DIR/$name.bin" > "$TEMP_DIR/$name.compiled" 2>&1
    if diff -u "$expected" "$TEMP_DIR/$name.compiled" > "$TEMP_DIR/$name.compiled.diff"; then
        passed=$((passed + 1))
    else
        echo "✗ $name (compiler)"
        cat "$TEMP_DIR/$name.compiled.diff"
        failed=$((failed + 1))
    fi
done

echo "Sandbox tests: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
