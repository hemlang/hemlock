#!/bin/bash
# Formatter test runner

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HEMLOCK="${SCRIPT_DIR}/../../hemlock"
PASSED=0
FAILED=0

echo "Running formatter tests..."
echo ""

for expected in "$SCRIPT_DIR"/*.expected; do
    test_name=$(basename "$expected" .expected)
    input="$SCRIPT_DIR/${test_name}.hml"

    if [ ! -f "$input" ]; then
        echo "⚠ Skipping $test_name: no input file"
        continue
    fi

    # Create temp copy
    temp=$(mktemp)
    cp "$input" "$temp"

    # Format it
    "$HEMLOCK" format "$temp" > /dev/null 2>&1

    # Compare
    if diff -q "$temp" "$expected" > /dev/null 2>&1; then
        echo "✓ $test_name"
        ((PASSED++))
    else
        echo "✗ $test_name"
        echo "  Expected:"
        head -5 "$expected" | sed 's/^/    /'
        echo "  Got:"
        head -5 "$temp" | sed 's/^/    /'
        ((FAILED++))
    fi

    rm -f "$temp"
done

# Test idempotency
echo ""
echo "Testing idempotency..."
for input in "$SCRIPT_DIR"/*.hml; do
    test_name=$(basename "$input" .hml)

    temp1=$(mktemp)
    temp2=$(mktemp)
    cp "$input" "$temp1"

    "$HEMLOCK" format "$temp1" > /dev/null 2>&1
    cp "$temp1" "$temp2"
    "$HEMLOCK" format "$temp2" > /dev/null 2>&1

    if diff -q "$temp1" "$temp2" > /dev/null 2>&1; then
        echo "✓ $test_name (idempotent)"
        ((PASSED++))
    else
        echo "✗ $test_name (not idempotent)"
        ((FAILED++))
    fi

    rm -f "$temp1" "$temp2"
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
