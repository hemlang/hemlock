#!/usr/bin/env bash
#
# Run the Hemlock formal verification proofs listed in proofs.txt.
#
#   verification/run.sh                 run everything
#   verification/run.sh utf8 rt_shr     run proofs whose name contains a pattern
#   JOBS=4 verification/run.sh          parallelism (default: nproc)
#   KEEP_LOGS=dir verification/run.sh   keep per-proof logs (counterexamples)
#
# Requires cbmc (>= 6) on PATH, plus bitwuzla for proofs flagged @bitwuzla in
# proofs.txt (CBMC's built-in SAT backend is ~20x slower on 64-bit
# multiply/divide; it is used as a fallback when bitwuzla is missing),
# and java + tla2tools.jar for the TLA+ models (TLA2TOOLS_JAR, default
# verification/.cache/tla2tools.jar). See README.md for setup.
#
# Exit status is non-zero if any proof's verdict differs from its expected
# verdict in proofs.txt, or a proof could not be run.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
MANIFEST="$HERE/proofs.txt"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
TLA2TOOLS_JAR="${TLA2TOOLS_JAR:-$HERE/.cache/tla2tools.jar}"
LOGDIR="${KEEP_LOGS:-$(mktemp -d)}"
mkdir -p "$LOGDIR"

# Properties checked on every CBMC proof, in addition to the harness asserts.
CBMC_CHECKS="--bounds-check --pointer-check --div-by-zero-check --undefined-shift-check
             --signed-overflow-check --pointer-overflow-check --unwinding-assertions"
CBMC_INCLUDES="-I$ROOT/include -I$ROOT/src -I$ROOT/src/frontend -I$ROOT/src/backends
               -I$ROOT/src/shared -I$ROOT/src/backends/interpreter
               -I$ROOT/runtime/include -I$ROOT/runtime/src -I$HERE/cbmc/harnesses"
CBMC_SOLVER="--sat-solver cadical"   # default for every proof; see @bitwuzla
command -v bitwuzla >/dev/null 2>&1 && CBMC_SOLVER="--bitwuzla"

declare -A VARS=()
PROOFS=()
while IFS= read -r line; do
    [[ -z "$line" || "$line" == \#* ]] && continue
    if [[ "$line" =~ ^([A-Z_]+)=(.*)$ ]]; then          # NAME=value macro
        VARS[${BASH_REMATCH[1]}]="${BASH_REMATCH[2]}"
        continue
    fi
    for k in "${!VARS[@]}"; do line="${line//\$$k/${VARS[$k]}}"; done
    # @pairs: expand into one proof per (left, right) integer type pair, so
    # the promoted type is a constant and the solver sees one width at a time.
    if [[ " $line " == *" @pairs"* ]]; then
        base="${line// @pairs/}"
        for lt in I8 I16 I32 I64 U8 U16 U32 U64; do
            for rt in I8 I16 I32 I64 U8 U16 U32 U64; do
                lt_l=$(tr 'A-Z' 'a-z' <<< "$lt"); rt_l=$(tr 'A-Z' 'a-z' <<< "$rt")
                PROOFS+=("${base%%|*}_${lt_l}_${rt_l}|${base#*|} -DLT=HML_TK_$lt -DRT=HML_TK_$rt")
            done
        done
        continue
    fi
    PROOFS+=("$line")
done < "$MANIFEST"

selected() {
    [[ $# -le 1 ]] && return 0
    local name="$1"; shift
    for pat in "$@"; do [[ "$name" == *"$pat"* ]] && return 0; done
    return 1
}

run_one() {
    local line="$1"
    IFS='|' read -r name expect tool target sources flags <<< "$line"
    local log="$LOGDIR/$name.log" verdict start=$SECONDS
    case "$tool" in
        cbmc)
            local srcs=() solver="--sat-solver cadical"
            if [[ " $flags " == *" @bitwuzla "* ]]; then
                solver="$CBMC_SOLVER"; flags="${flags//@bitwuzla/}"
            fi
            for s in $sources; do
                if [[ "$s" == @* ]]; then srcs+=("$HERE/cbmc/${s#@}"); else srcs+=("$ROOT/$s"); fi
            done
            # @unreachable=f1|f2: replace those functions' bodies with
            # assert(false); assume(false). Sound way to cut code the harness
            # proves is never reached (a reached stub is a reported failure).
            local unreach="" defs=() rest=()
            for f in $flags; do
                case "$f" in
                    @unreachable=*) unreach="${f#@unreachable=}" ;;
                    -D*|-I*) defs+=("$f") ;;
                    *) rest+=("$f") ;;
                esac
            done
            local work; work="$(mktemp -d)"
            # shellcheck disable=SC2086
            {
                goto-cc -o "$work/a.goto" "$HERE/cbmc/harnesses/$target" "${srcs[@]}" \
                    $CBMC_INCLUDES -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE "${defs[@]}" \
                    --function harness &&
                if [[ -n "$unreach" ]]; then
                    local rm_args=()
                    for f in ${unreach//|/ }; do rm_args+=(--remove-function-body "$f"); done
                    goto-instrument "${rm_args[@]}" "$work/a.goto" "$work/b.goto" &&
                    goto-instrument --generate-function-body "($unreach)" \
                        --generate-function-body-options assert-false-assume-false \
                        "$work/b.goto" "$work/a.goto"
                fi &&
                cbmc "$work/a.goto" --function harness $CBMC_CHECKS $solver "${rest[@]}" --trace
            } > "$log" 2>&1
            case $? in 0) verdict=pass ;; 10) verdict=fail ;; *) verdict=error ;; esac
            rm -rf "$work"
            ;;
        tlc)
            local meta; meta="$(mktemp -d)"
            (cd "$HERE/tla" && java -XX:+UseParallelGC -cp "$TLA2TOOLS_JAR" tlc2.TLC \
                -workers 1 -metadir "$meta" -config "$target" "$sources" $flags) > "$log" 2>&1
            # TLC: 0 = no error, 11 = deadlock, 12 = safety violation, 13 = liveness
            case $? in 0) verdict=pass ;; 11|12|13) verdict=fail ;; *) verdict=error ;; esac
            rm -rf "$meta"
            ;;
        *) verdict=error; echo "unknown tool '$tool'" > "$log" ;;
    esac
    local status=OK
    [[ "$verdict" != "$expect" ]] && status=MISMATCH
    printf '%-8s %-30s expected=%-4s got=%-5s %4ss\n' "$status" "$name" "$expect" "$verdict" \
        "$((SECONDS - start))"
}
export -f run_one
export HERE ROOT LOGDIR TLA2TOOLS_JAR CBMC_CHECKS CBMC_INCLUDES CBMC_SOLVER

if ! command -v cbmc >/dev/null 2>&1; then echo "cbmc not found on PATH" >&2; exit 2; fi
[[ "$CBMC_SOLVER" != "--bitwuzla" ]] && echo "note: bitwuzla not found; @bitwuzla proofs fall back to CaDiCaL (slow)" >&2

RESULTS="$LOGDIR/results.txt"
for p in "${PROOFS[@]}"; do
    name="${p%%|*}"
    selected "$name" "$@" && printf '%s\0' "$p"
done | xargs -0 -P "$JOBS" -I{} bash -c 'run_one "$1"' _ {} | tee "$RESULTS"

total=$(wc -l < "$RESULTS")
bad=$(grep -c '^MISMATCH' "$RESULTS")
echo "----"
echo "$total proofs run, $((total - bad)) matched expectations, $bad mismatched (logs: $LOGDIR)"
[[ "$bad" -eq 0 && "$total" -gt 0 ]]
