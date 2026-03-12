#!/bin/bash
# HemBench - LLM Benchmark Runner for Hemlock Code Generation
#
# Usage:
#   ./run_benchmark.sh [OPTIONS]
#
# Options:
#   --level LEVEL       Run only tasks for a specific level (L1-L6)
#   --task TASK_ID      Run a single task by ID (e.g., L1-E-01)
#   --solutions DIR     Directory containing .hml solutions (default: solutions/)
#   --hemlock PATH      Path to hemlock interpreter (default: ../hemlock)
#   --timeout MS        Default timeout in milliseconds (default: 10000)
#   --json              Output results as JSON
#   --validate          Validate reference solutions against expected output
#   --help              Show this help message

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
DIM='\033[2m'
BOLD='\033[1m'
NC='\033[0m'

# Defaults
HEMLOCK="${PROJECT_ROOT}/hemlock"
SOLUTIONS_DIR="${SCRIPT_DIR}/solutions"
TASKS_DIR="${SCRIPT_DIR}/tasks"
DEFAULT_TIMEOUT=10000
FILTER_LEVEL=""
FILTER_TASK=""
JSON_OUTPUT=false
VALIDATE_MODE=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) FILTER_LEVEL="$2"; shift 2 ;;
        --task) FILTER_TASK="$2"; shift 2 ;;
        --solutions) SOLUTIONS_DIR="$2"; shift 2 ;;
        --hemlock) HEMLOCK="$2"; shift 2 ;;
        --timeout) DEFAULT_TIMEOUT="$2"; shift 2 ;;
        --json) JSON_OUTPUT=true; shift ;;
        --validate) VALIDATE_MODE=true; shift ;;
        --help)
            head -15 "$0" | tail -13
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Verify hemlock exists
if [[ ! -x "$HEMLOCK" ]]; then
    echo -e "${RED}Error: Hemlock interpreter not found at $HEMLOCK${NC}"
    echo "Build it with: make"
    exit 1
fi

# Timing helper
get_time_ms() {
    if command -v python3 &>/dev/null; then
        python3 -c 'import time; print(int(time.time() * 1000))'
    else
        date +%s%N | cut -b1-13
    fi
}

# Results tracking
TOTAL=0
PASSED=0
FAILED=0
PARSE_ERRORS=0
RUNTIME_ERRORS=0
RESULTS_JSON="["

# Run a single task
run_task() {
    local task_json="$1"
    local task_id
    local expected_output
    local timeout_ms
    local validator

    task_id=$(python3 -c "import json; d=json.load(open('$task_json')); print(d['id'])")
    expected_output=$(python3 -c "import json,sys; d=json.load(open('$task_json')); sys.stdout.write(d['expected_output'])")
    timeout_ms=$(python3 -c "import json; d=json.load(open('$task_json')); print(d.get('timeout_ms', $DEFAULT_TIMEOUT))")
    validator=$(python3 -c "import json; d=json.load(open('$task_json')); print(d.get('validator', 'exact_match'))")
    local title
    title=$(python3 -c "import json; d=json.load(open('$task_json')); print(d['title'])")
    local difficulty
    difficulty=$(python3 -c "import json; d=json.load(open('$task_json')); print(d['difficulty'])")
    local level
    level=$(python3 -c "import json; d=json.load(open('$task_json')); print(d['level'])")

    # Apply filters
    if [[ -n "$FILTER_LEVEL" && "$level" != "$FILTER_LEVEL" ]]; then
        return
    fi
    if [[ -n "$FILTER_TASK" && "$task_id" != "$FILTER_TASK" ]]; then
        return
    fi

    # Find solution file
    local solution_file=""
    solution_file=$(ls ${SOLUTIONS_DIR}/${task_id}*.hml 2>/dev/null | head -1 || true)

    TOTAL=$((TOTAL + 1))

    if [[ -z "$solution_file" || ! -f "$solution_file" ]]; then
        TOTAL=$((TOTAL - 1))  # Don't count skipped tasks
        if [[ "$JSON_OUTPUT" == "false" ]]; then
            echo -e "  ${DIM}[$task_id]${NC} ${title} ${DIM}($difficulty)${NC} ... ${YELLOW}SKIP${NC} (no solution)"
        fi
        RESULTS_JSON="${RESULTS_JSON}{\"id\":\"$task_id\",\"title\":\"$title\",\"status\":\"skip\",\"reason\":\"no solution file\"},"
        return
    fi

    # Run the solution
    local timeout_sec=$(( (timeout_ms + 999) / 1000 ))
    local start_time
    start_time=$(get_time_ms)

    local actual_output
    local exit_code
    actual_output=$(timeout "$timeout_sec" "$HEMLOCK" "$solution_file" 2>&1) || exit_code=$?
    exit_code=${exit_code:-0}

    local end_time
    end_time=$(get_time_ms)
    local duration_ms=$((end_time - start_time))

    # Evaluate result
    local status="fail"
    local parses=true
    local runs=true
    local correct=false

    if [[ $exit_code -eq 124 ]]; then
        status="timeout"
        runs=false
    elif [[ $exit_code -eq 139 ]]; then
        status="segfault"
        runs=false
    elif [[ $exit_code -ne 0 ]]; then
        # Check if it's a parse error vs runtime error
        if echo "$actual_output" | grep -q "Parse error\|Syntax error\|Unexpected token"; then
            parses=false
            runs=false
            PARSE_ERRORS=$((PARSE_ERRORS + 1))
        else
            runs=true
            RUNTIME_ERRORS=$((RUNTIME_ERRORS + 1))
        fi
    fi

    if [[ $exit_code -eq 0 ]]; then
        # Compare output
        case "$validator" in
            exact_match)
                # Trim trailing whitespace/newlines for comparison
                local trimmed_actual trimmed_expected
                trimmed_actual=$(echo "$actual_output" | sed 's/[[:space:]]*$//')
                trimmed_expected=$(echo "$expected_output" | sed 's/[[:space:]]*$//')
                if [[ "$trimmed_actual" == "$trimmed_expected" ]]; then
                    correct=true
                    status="pass"
                fi
                ;;
            starts_with)
                local prefix
                prefix=$(python3 -c "import json; d=json.load(open('$task_json')); print(d.get('validator_config',{}).get('required_prefix',''))")
                if [[ "$actual_output" == "$prefix"* ]]; then
                    correct=true
                    status="pass"
                fi
                ;;
            *)
                # Default: exact match
                if [[ "$actual_output" == "$expected_output" ]]; then
                    correct=true
                    status="pass"
                fi
                ;;
        esac
    fi

    # Update counters
    if [[ "$status" == "pass" ]]; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi

    # Output
    if [[ "$JSON_OUTPUT" == "false" ]]; then
        local color
        case "$status" in
            pass) color="$GREEN" ;;
            timeout) color="$YELLOW" ;;
            segfault) color="$RED" ;;
            *) color="$RED" ;;
        esac

        local status_icon
        case "$status" in
            pass) status_icon="✓" ;;
            skip) status_icon="○" ;;
            timeout) status_icon="⏱" ;;
            *) status_icon="✗" ;;
        esac

        printf "  ${DIM}[%s]${NC} %-35s ${DIM}(%s)${NC}  ${color}%s %s${NC}  ${DIM}%dms${NC}\n" \
            "$task_id" "$title" "$difficulty" "$status_icon" "$status" "$duration_ms"

        # Show diff on failure
        if [[ "$status" != "pass" && "$status" != "skip" ]]; then
            if [[ "$runs" == "false" ]]; then
                echo -e "         ${DIM}Error: $(echo "$actual_output" | head -1)${NC}"
            else
                echo -e "         ${DIM}Expected: $(echo "$expected_output" | head -1)${NC}"
                echo -e "         ${DIM}Got:      $(echo "$actual_output" | head -1)${NC}"
            fi
        fi
    fi

    # JSON result
    local lines
    lines=$(wc -l < "$solution_file" | tr -d ' ')
    RESULTS_JSON="${RESULTS_JSON}{\"id\":\"$task_id\",\"title\":\"$title\",\"difficulty\":\"$difficulty\",\"level\":\"$level\",\"status\":\"$status\",\"parses\":$parses,\"runs\":$runs,\"correct\":$correct,\"time_ms\":$duration_ms,\"lines\":$lines},"
}

# Main execution
if [[ "$JSON_OUTPUT" == "false" ]]; then
    echo ""
    echo -e "${BOLD}╔══════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}║          HemBench - Hemlock LLM Benchmark        ║${NC}"
    echo -e "${BOLD}╚══════════════════════════════════════════════════╝${NC}"
    echo ""

    if [[ "$VALIDATE_MODE" == "true" ]]; then
        echo -e "${CYAN}Mode: Validating reference solutions${NC}"
    else
        echo -e "${CYAN}Solutions dir: ${SOLUTIONS_DIR}${NC}"
    fi
    echo -e "${CYAN}Interpreter:   ${HEMLOCK}${NC}"
    echo ""
fi

# Process each level
for level_dir in "$TASKS_DIR"/L*; do
    if [[ ! -d "$level_dir" ]]; then continue; fi

    level_name=$(basename "$level_dir")

    # Apply level filter
    if [[ -n "$FILTER_LEVEL" ]]; then
        level_prefix="${FILTER_LEVEL}_"
        if [[ "$level_name" != "${FILTER_LEVEL}_"* && "$level_name" != "$FILTER_LEVEL" ]]; then
            continue
        fi
    fi

    if [[ "$JSON_OUTPUT" == "false" ]]; then
        echo -e "${BOLD}${BLUE}── $level_name ──${NC}"
    fi

    for task_json in "$level_dir"/*.json; do
        if [[ ! -f "$task_json" ]]; then continue; fi
        run_task "$task_json"
    done

    if [[ "$JSON_OUTPUT" == "false" ]]; then
        echo ""
    fi
done

# Summary
if [[ "$JSON_OUTPUT" == "false" ]]; then
    echo -e "${BOLD}══════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}Summary${NC}"
    echo -e "  Total tasks:    $TOTAL"
    echo -e "  ${GREEN}Passed:${NC}         $PASSED"
    echo -e "  ${RED}Failed:${NC}         $FAILED"
    if [[ $PARSE_ERRORS -gt 0 ]]; then
        echo -e "  ${YELLOW}Parse errors:${NC}   $PARSE_ERRORS"
    fi
    if [[ $RUNTIME_ERRORS -gt 0 ]]; then
        echo -e "  ${YELLOW}Runtime errors:${NC} $RUNTIME_ERRORS"
    fi

    if [[ $TOTAL -gt 0 ]]; then
        local_score=$(python3 -c "print(f'{$PASSED/$TOTAL*100:.1f}%')" 2>/dev/null || echo "N/A")
        echo -e "  ${BOLD}Score:${NC}          $local_score"
    fi
    echo -e "${BOLD}══════════════════════════════════════════════════${NC}"
fi

# JSON output
if [[ "$JSON_OUTPUT" == "true" ]]; then
    # Remove trailing comma and close array
    RESULTS_JSON="${RESULTS_JSON%,}]"
    python3 -c "
import json, sys
results = json.loads('$RESULTS_JSON')
output = {
    'benchmark': 'HemBench',
    'version': '1.0',
    'total': $TOTAL,
    'passed': $PASSED,
    'failed': $FAILED,
    'score': round($PASSED / max($TOTAL, 1), 4),
    'results': results
}
print(json.dumps(output, indent=2))
"
fi

# Exit code reflects success
if [[ $FAILED -gt 0 ]]; then
    exit 1
fi
exit 0
