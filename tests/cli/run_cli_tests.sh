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

# Spawned tasks must inherit the spawner's sandbox. exec_context_new() starts
# unrestricted, so a task that skipped inheritance could run commands and read
# outside the sandbox root even though the main thread could not.
sbx_dir=$(mktemp -d)
sbx_outside=$(mktemp -d)
trap 'rm -rf "$sbx_dir" "$sbx_outside"' EXIT
echo secret > "$sbx_outside/secret.txt"

sbx_code='
import { read_file } from "@stdlib/fs";
async fn read_outside(p: string): string {
    try { return "READ:" + read_file(p).trim(); } catch (e) { return "BLOCKED"; }
}
async fn run_cmd(): string {
    try { __exec_argv(["echo", "x"]); return "EXEC"; } catch (e) { return "BLOCKED"; }
}
print("task_read=" + (await spawn(read_outside, "'"$sbx_outside"'/secret.txt")));
print("task_exec=" + (await spawn(run_cmd)));
'
output=$("$HEMLOCK" --sandbox "$sbx_dir" -e "$sbx_code" 2>&1) || fail "sandboxed task probe should run"
assert_contains "$output" "task_read=BLOCKED" "spawned task should inherit the sandbox file-read root"
assert_contains "$output" "task_exec=BLOCKED" "spawned task should inherit the sandbox process restriction"
assert_not_contains "$output" "secret" "spawned task must not read outside the sandbox root"

echo "CLI tests passed"
