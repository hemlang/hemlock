#!/bin/bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HEMLOCK="$PROJECT_ROOT/hemlock"

fail() {
    echo "✗ $1" >&2
    exit 1
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local description="$3"

    if [[ "$haystack" != *"$needle"* ]]; then
        fail "$description (expected output to contain: $needle)"
    fi
}

assert_not_contains() {
    local haystack="$1"
    local needle="$2"
    local description="$3"

    if [[ "$haystack" == *"$needle"* ]]; then
        fail "$description (did not expect output to contain: $needle)"
    fi
}

run_help_case() {
    local flag="$1"
    local output

    output=$("$HEMLOCK" -e "$flag" 2>&1) || fail "hemlock -e $flag should exit successfully"
    assert_contains "$output" "USAGE:" "hemlock -e $flag should print help"
    assert_not_contains "$output" "Parse failed" "hemlock -e $flag should not parse the help flag as code"
}

run_help_case "--help"
run_help_case "-h"

output=$("$HEMLOCK" -e 'print("ok");' 2>&1) || fail "hemlock -e should still execute inline code"
assert_contains "$output" "ok" "hemlock -e should execute inline code"

echo "CLI tests passed"
