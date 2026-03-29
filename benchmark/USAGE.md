# HemBench LLM — Usage Guide

Automated LLM benchmarking for Hemlock code generation. Spins up a local llama-server, sends each benchmark task to the model, runs the generated code through the Hemlock interpreter, and scores the results.

## Prerequisites

- Python 3.8+
- [llama.cpp](https://github.com/ggerganov/llama.cpp) (`llama-server` on `$PATH` or specify with `--llama-server`)
- A built Hemlock interpreter (`make` in the project root)
- One or more GGUF model files

## Quick Start

```bash
# Benchmark a single model (zero-shot, all 38 tasks)
python3 benchmark/hembench_llm.py --model ~/AI/Models/my-model.Q4_K_M.gguf

# Benchmark every .gguf in a directory and get a comparison table
python3 benchmark/hembench_llm.py --model-dir ~/AI/Models/
```

## How It Works

For each model, the runner:

1. Starts `llama-server` with the GGUF file on a local port (default 8199).
2. Waits for the server health check to pass.
3. Iterates through the 38 benchmark tasks (L1–L6).
4. Sends each task prompt to the model via the OpenAI-compatible `/v1/chat/completions` endpoint.
5. Extracts Hemlock source code from the response (strips markdown fences, preamble text, etc.).
6. Writes the generated `.hml` file to `benchmark/results/<model>/solutions/`.
7. Runs it through `./hemlock` and compares stdout against the expected output.
8. Prints a per-level score breakdown and weighted overall score.
9. Saves full results as JSON.
10. Shuts down `llama-server` and moves to the next model (if using `--model-dir`).

## Prompt Variants

The `--variant` flag controls what context the model receives alongside each task prompt.

| Variant | What the model sees | Use case |
|---------|-------------------|----------|
| `zero-shot` (default) | Task prompt only | Test what a fine-tune actually learned |
| `compact-doc` | Task prompt + condensed Hemlock reference (~4K tokens) | Fits small context windows, tests doc-following |
| `doc-guided` | Task prompt + full CLAUDE.md (~10K tokens) | Tests doc-following with complete spec |
| `few-shot` | Task prompt + CLAUDE.md + 2–3 solved examples | Maximum context, tests in-context learning |

For evaluating fine-tuned models, `zero-shot` is the most informative — it measures what the model internalized from training rather than its ability to follow documentation at inference time.

The gap between `zero-shot` and `doc-guided` scores reveals how much the model relies on documentation prompting vs. genuine learned knowledge.

## Scoring

Each task is scored pass/fail based on exact output match against the expected result. Scores are aggregated per-level and combined into a weighted overall score.

| Level | Name | Tasks | Weight | Focus |
|-------|------|-------|--------|-------|
| L1 | Syntax & Basics | 9 | 0.10 | Variables, control flow, closures, pattern matching |
| L2 | Stdlib Usage | 5 | 0.15 | HashMap, JSON, math, encoding, datetime |
| L3 | Algorithms | 7 | 0.20 | Sorting, BST, BFS, Dijkstra, DP, trie |
| L4 | Systems Programming | 7 | 0.25 | alloc/free, defer, async/spawn, channels, atomics |
| L5 | Translation | 5 | 0.15 | Python/JS/C/Go/Rust → Hemlock |
| L6 | Debugging | 5 | 0.15 | Fix syntax, logic, type, memory, concurrency bugs |

L4 carries the highest weight (25%) because systems programming is where Hemlock diverges most from mainstream languages and where memorization helps least.

## Task Filtering

```bash
# Run only L1 tasks (syntax basics, good for sanity checks)
python3 benchmark/hembench_llm.py --model model.gguf --level L1

# Run a single task by ID
python3 benchmark/hembench_llm.py --model model.gguf --task L1-E-01

# Dry run — list all tasks without calling the LLM
python3 benchmark/hembench_llm.py --model model.gguf --dry-run
```

## Server Configuration

```bash
# Custom llama-server binary location
python3 benchmark/hembench_llm.py --model model.gguf --llama-server /opt/llama/llama-server

# Change the port (default: 8199)
python3 benchmark/hembench_llm.py --model model.gguf --port 8080

# Set context size (default: 32768; increase for doc-guided with large tasks)
python3 benchmark/hembench_llm.py --model model.gguf --ctx-size 16384

# CPU-only inference (default: -1, all layers on GPU)
python3 benchmark/hembench_llm.py --model model.gguf --n-gpu-layers 0
```

## Generation Parameters

```bash
# Higher temperature for more creative solutions (default: 0.2)
python3 benchmark/hembench_llm.py --model model.gguf --temperature 0.7

# More tokens for complex tasks (default: 2048)
python3 benchmark/hembench_llm.py --model model.gguf --max-tokens 4096
```

## Debugging

```bash
# Verbose mode — shows raw LLM response and extracted code for every task
python3 benchmark/hembench_llm.py --model model.gguf --verbose

# Verbose on a single task for quick iteration
python3 benchmark/hembench_llm.py --model model.gguf --task L3-M-01 -v
```

In addition to terminal output, the runner saves two files per task in `benchmark/results/<model>/solutions/`:

- `<task-id>_<name>.hml` — The extracted Hemlock code that was actually run.
- `<task-id>_raw.txt` — The full unprocessed LLM response, for inspecting preamble text, markdown wrapping, or other model quirks.

## Comparing Models

Pass `--model-dir` to benchmark every `.gguf` file in a directory sequentially. After all models complete, a comparison table is printed and a combined `comparison.json` is saved.

```bash
python3 benchmark/hembench_llm.py --model-dir ~/AI/Models/hemlock-finetunes/
```

Example output:

```
══════════════════════════════════════════════════════════════════════
  Model Comparison
══════════════════════════════════════════════════════════════════════
  Level       model-a-Q4_K_M           model-b-Q8_0
  ────────────────────────────────────────────────────────────────────
  L1           88.9% (8/9)             100.0% (9/9)
  L2           60.0% (3/5)              80.0% (4/5)
  L3           42.9% (3/7)              57.1% (4/7)
  L4           28.6% (2/7)              42.9% (3/7)
  L5           40.0% (2/5)              60.0% (3/5)
  L6           60.0% (3/5)              60.0% (3/5)
  ────────────────────────────────────────────────────────────────────
  OVERALL      49.3%                    63.1%
══════════════════════════════════════════════════════════════════════
```

## Output Files

All results are saved under `benchmark/results/<model-name>/`:

```
benchmark/results/
└── my-model-Q4_K_M/
    ├── results.json          # Full structured results with per-task scores
    └── solutions/
        ├── L1-E-01_fizzbuzz.hml
        ├── L1-E-01_raw.txt
        ├── L1-E-02_recursive_fibonacci.hml
        ├── L1-E-02_raw.txt
        └── ...
```

The `results.json` file contains everything needed for further analysis:

```json
{
  "model": "my-model-Q4_K_M",
  "variant": "zero-shot",
  "timestamp": "2026-03-29T...",
  "results": [
    {
      "id": "L1-E-01",
      "title": "FizzBuzz",
      "difficulty": "easy",
      "status": "pass",
      "correct": true,
      "parses": true,
      "runs": true,
      "gen_time_s": 9.7,
      "lines": 12
    }
  ],
  "scores": {
    "levels": { "L1": { "total": 9, "correct": 8, "score": 0.8889 } },
    "overall": 0.6234
  }
}
```

## Tips

- Start with `--level L1` to quickly verify your setup works before running the full suite.
- Use `--dry-run` to confirm which tasks will be included before a long run.
- A full 38-task run takes roughly 10–20 minutes depending on model size and hardware.
- The `results/` directory is gitignored — generated solutions and scores are local only.
- For reproducible benchmarks, use `--temperature 0` (greedy decoding).
