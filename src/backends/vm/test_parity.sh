#!/bin/bash

PASS=0
FAIL=0
FAILED_TESTS=""

cd /home/user/hemlock

for f in tests/parity/language/*.hml; do
    expected="${f%.hml}.expected"
    if [ -f "$expected" ]; then
        name=$(basename "$f")
        output=$(./src/backends/vm/hemlockvm "$f" 2>&1)
        exp=$(cat "$expected")
        if [ "$output" = "$exp" ]; then
            echo "✓ $name"
            PASS=$((PASS + 1))
        else
            echo "✗ $name"
            FAIL=$((FAIL + 1))
            FAILED_TESTS="$FAILED_TESTS $name"
        fi
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ -n "$FAILED_TESTS" ]; then
    echo "Failed tests:$FAILED_TESTS"
fi
