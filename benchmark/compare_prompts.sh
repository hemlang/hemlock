#!/usr/bin/env bash
# Compare system prompts against a fixed model.
# Usage: ./compare_prompts.sh <model.gguf>
set -u

MODEL="${1:-/home/nbeerbower/AI/hembench/Hemlock-Apothecary-7B-Q8_0.gguf}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROMPTS_DIR="$SCRIPT_DIR/system_prompts"
RESULTS_DIR="$SCRIPT_DIR/prompt_comparison_results"
mkdir -p "$RESULTS_DIR"

MODEL_NAME=$(basename "$MODEL" .gguf)

echo "Model: $MODEL_NAME"
echo "==========================="

# Baseline first (no --system-prompt-file = default)
echo -e "\n=== baseline (default prompt) ==="
python3 "$SCRIPT_DIR/hembench_llm.py" --model "$MODEL" 2>&1 | tee "$RESULTS_DIR/${MODEL_NAME}_baseline.log" | grep -E 'Weighted Overall|L[1-6]  [█░]'

# Loop through each prompt file
for prompt_file in "$PROMPTS_DIR"/*.txt; do
    name=$(basename "$prompt_file" .txt)
    echo -e "\n=== $name ==="
    python3 "$SCRIPT_DIR/hembench_llm.py" \
        --model "$MODEL" \
        --system-prompt-file "$prompt_file" \
        2>&1 | tee "$RESULTS_DIR/${MODEL_NAME}_${name}.log" | grep -E 'Weighted Overall|L[1-6]  [█░]'
done

echo -e "\n\n=========================="
echo "Summary (overall scores):"
echo "=========================="
for log in "$RESULTS_DIR"/${MODEL_NAME}_*.log; do
    variant=$(basename "$log" .log | sed "s/^${MODEL_NAME}_//")
    score=$(grep 'Weighted Overall' "$log" | sed -E 's/.*Weighted Overall: [^0-9]*([0-9.]+%).*/\1/')
    printf "%-40s %s\n" "$variant" "$score"
done
