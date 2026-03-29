#!/usr/bin/env python3
"""
HemBench LLM — End-to-end benchmark runner for evaluating LLMs on Hemlock code generation.

Spins up llama-server for each GGUF model, sends every HemBench task as a prompt,
saves the generated .hml files, runs them through the Hemlock interpreter, and
compares output against expected results.

Usage:
    # Benchmark a single model
    python3 hembench_llm.py --model ~/AI/Models/my-model.Q4_K_M.gguf

    # Benchmark all .gguf files in a directory
    python3 hembench_llm.py --model-dir ~/AI/Models/

    # Filter by level
    python3 hembench_llm.py --model ~/AI/Models/model.gguf --level L1

    # Custom llama-server path/port
    python3 hembench_llm.py --model ~/AI/Models/model.gguf --llama-server /usr/local/bin/llama-server --port 8234

    # Dry run (don't call LLM, just show what would happen)
    python3 hembench_llm.py --model ~/AI/Models/model.gguf --dry-run

    # Specify prompt variant
    python3 hembench_llm.py --model ~/AI/Models/model.gguf --variant doc-guided
"""

import argparse
import glob
import json
import os
import re
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

# ─── Constants ───────────────────────────────────────────────────────────────

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
TASKS_DIR = SCRIPT_DIR / "tasks"
HEMLOCK_BIN = PROJECT_ROOT / "hemlock"
CLAUDE_MD = PROJECT_ROOT / "CLAUDE.md"

DEFAULT_PORT = 8199  # unlikely to collide
SERVER_STARTUP_TIMEOUT = 120  # seconds to wait for llama-server
TASK_TIMEOUT_SEC = 15  # per-task hemlock execution timeout
LLM_TIMEOUT_SEC = 120  # per-task LLM generation timeout
MAX_TOKENS = 2048
TEMPERATURE = 0.2  # low temp for deterministic code gen

LEVEL_WEIGHTS = {
    "L1": 0.10,
    "L2": 0.15,
    "L3": 0.20,
    "L4": 0.25,
    "L5": 0.15,
    "L6": 0.15,
}

# ─── Colors ──────────────────────────────────────────────────────────────────

class C:
    RED = "\033[0;31m"
    GREEN = "\033[0;32m"
    YELLOW = "\033[0;33m"
    BLUE = "\033[0;34m"
    CYAN = "\033[0;36m"
    MAGENTA = "\033[0;35m"
    DIM = "\033[2m"
    BOLD = "\033[1m"
    NC = "\033[0m"


# ─── Utilities ───────────────────────────────────────────────────────────────

def log(msg, color=""):
    print(f"{color}{msg}{C.NC}", flush=True)


def log_dim(msg):
    log(msg, C.DIM)


def model_short_name(path: str) -> str:
    """Extract a short name from a GGUF filename."""
    name = Path(path).stem
    # Remove common suffixes for display
    for suffix in [".gguf", "-GGUF"]:
        name = name.replace(suffix, "")
    return name


def load_hemlock_docs():
    """Load CLAUDE.md and compact reference as language documentation context."""
    full = ""
    if CLAUDE_MD.exists():
        full = CLAUDE_MD.read_text()
    else:
        alt = PROJECT_ROOT / "docs" / "README.md"
        if alt.exists():
            full = alt.read_text()

    compact_path = SCRIPT_DIR / "HEMLOCK_COMPACT.md"
    compact = compact_path.read_text() if compact_path.exists() else full
    return full, compact


def load_tasks(level_filter: str = "", task_filter: str = "") :
    """Load all benchmark task JSON files, optionally filtered."""
    tasks = []
    for level_dir in sorted(TASKS_DIR.iterdir()):
        if not level_dir.is_dir():
            continue
        level_name = level_dir.name.split("_")[0]  # "L1_syntax" -> "L1"

        if level_filter and level_name != level_filter:
            continue

        for task_file in sorted(level_dir.glob("*.json")):
            with open(task_file) as f:
                task = json.load(f)
            task["_file"] = str(task_file)

            if task_filter and task["id"] != task_filter:
                continue

            tasks.append(task)
    return tasks


# ─── Prompt Construction ─────────────────────────────────────────────────────

def build_system_prompt(variant: str, docs: str, compact_docs: str) -> str:
    """Build the system prompt based on variant."""
    base = (
        "You are a code generation assistant. You write programs in the Hemlock programming language.\n"
        "IMPORTANT: Respond ONLY with the Hemlock source code. No markdown fences, no explanations, "
        "no comments about the code — just the raw .hml program that can be run directly.\n"
        "Do NOT wrap your code in ```hemlock``` or ``` blocks.\n"
    )

    if variant == "zero-shot":
        return base

    if variant == "compact-doc":
        return base + (
            "\n\nHere is a compact Hemlock language reference:\n\n"
            "--- BEGIN HEMLOCK REFERENCE ---\n"
            f"{compact_docs}\n"
            "--- END HEMLOCK REFERENCE ---\n"
        )

    if variant == "doc-guided":
        return base + (
            "\n\nHere is the complete Hemlock language specification:\n\n"
            "--- BEGIN HEMLOCK DOCUMENTATION ---\n"
            f"{docs}\n"
            "--- END HEMLOCK DOCUMENTATION ---\n"
        )

    if variant == "few-shot":
        # Include a couple of solved examples
        examples = _get_few_shot_examples()
        return base + (
            "\n\nHere is the Hemlock language specification:\n\n"
            "--- BEGIN HEMLOCK DOCUMENTATION ---\n"
            f"{docs}\n"
            "--- END HEMLOCK DOCUMENTATION ---\n\n"
            "Here are some example Hemlock programs for reference:\n\n"
            f"{examples}\n"
        )

    # Default: zero-shot
    return build_system_prompt("zero-shot", docs, compact_docs)


def _get_few_shot_examples() -> str:
    """Load reference solutions as few-shot examples."""
    solutions_dir = SCRIPT_DIR / "solutions"
    examples = []
    for sol_file in sorted(solutions_dir.glob("*.hml"))[:3]:
        code = sol_file.read_text()
        examples.append(f"// Example: {sol_file.stem}\n{code}")
    return "\n\n".join(examples) if examples else "(no examples available)"


def build_user_prompt(task: dict) -> str:
    """Build the user-facing prompt for a single task."""
    prompt = task["prompt"]

    # For debugging tasks, include the buggy code prominently
    if "buggy_code" in task:
        prompt += f"\n\nBuggy code:\n{task['buggy_code']}"

    # For translation tasks, source is usually embedded in the prompt already
    # Add expected output hint
    if task.get("expected_output"):
        prompt += f"\n\nThe program should produce this exact output:\n{task['expected_output'].rstrip()}"

    return prompt


# ─── LLM Server Management ──────────────────────────────────────────────────

class LlamaServer:
    """Manages a llama-server subprocess lifecycle."""

    def __init__(self, model_path: str, llama_bin: str = "llama-server",
                 port: int = DEFAULT_PORT, n_ctx: int = 8192, n_gpu_layers: int = -1):
        self.model_path = model_path
        self.llama_bin = llama_bin
        self.port = port
        self.n_ctx = n_ctx
        self.n_gpu_layers = n_gpu_layers
        self.process = None
        self.base_url = f"http://127.0.0.1:{port}"

    def start(self) -> bool:
        """Start llama-server and wait for it to be ready."""
        cmd = [
            self.llama_bin,
            "--model", self.model_path,
            "--port", str(self.port),
            "--ctx-size", str(self.n_ctx),
            "--n-gpu-layers", str(self.n_gpu_layers),
            "--log-disable",
        ]

        log(f"  Starting llama-server on port {self.port}...", C.DIM)
        log(f"  Model: {Path(self.model_path).name}", C.DIM)

        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                preexec_fn=os.setsid,
            )
        except FileNotFoundError:
            log(f"  ERROR: '{self.llama_bin}' not found. Install llama.cpp or provide --llama-server path.", C.RED)
            return False

        # Wait for /health to return OK
        start = time.time()
        while time.time() - start < SERVER_STARTUP_TIMEOUT:
            if self.process.poll() is not None:
                stderr = self.process.stderr.read().decode() if self.process.stderr else ""
                log(f"  ERROR: llama-server exited prematurely (code {self.process.returncode})", C.RED)
                if stderr:
                    for line in stderr.strip().split("\n")[-5:]:
                        log(f"    {line}", C.DIM)
                return False
            try:
                req = urllib.request.Request(f"{self.base_url}/health", method="GET")
                with urllib.request.urlopen(req, timeout=2) as resp:
                    data = json.loads(resp.read())
                    if data.get("status") == "ok":
                        log(f"  Server ready! ({time.time() - start:.1f}s)", C.GREEN)
                        return True
            except (urllib.error.URLError, ConnectionRefusedError, OSError, json.JSONDecodeError):
                pass
            time.sleep(1)

        log(f"  ERROR: Server didn't become ready within {SERVER_STARTUP_TIMEOUT}s", C.RED)
        self.stop()
        return False

    def stop(self):
        """Stop the llama-server process."""
        if self.process and self.process.poll() is None:
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
                self.process.wait(timeout=10)
            except (ProcessLookupError, subprocess.TimeoutExpired, PermissionError):
                try:
                    os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    pass
            self.process = None

    def generate(self, system_prompt: str, user_prompt: str,
                 max_tokens: int = MAX_TOKENS, temperature: float = TEMPERATURE,
                 verbose: bool = False):
        """Send a chat completion request and return (extracted_code, raw_response)."""
        payload = {
            "model": "local",
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            "max_tokens": max_tokens,
            "temperature": temperature,
        }

        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            f"{self.base_url}/v1/chat/completions",
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )

        try:
            with urllib.request.urlopen(req, timeout=LLM_TIMEOUT_SEC) as resp:
                result = json.loads(resp.read())
                content = result["choices"][0]["message"]["content"]
                finish_reason = result["choices"][0].get("finish_reason", "unknown")
                if verbose:
                    log(f"\n    ── raw LLM response ({len(content)} chars, finish: {finish_reason}) ──", C.DIM)
                    preview = content[:500]
                    for line in preview.split("\n"):
                        log(f"    │ {line}", C.DIM)
                    if len(content) > 500:
                        log(f"    │ ... ({len(content) - 500} more chars)", C.DIM)
                    log(f"    ── end raw ──", C.DIM)
                extracted = _extract_code(content)
                return extracted, content
        except urllib.error.HTTPError as e:
            body = e.read().decode() if e.fp else ""
            log(f"    HTTP {e.code}: {body[:200]}", C.RED)
            return None, None
        except (urllib.error.URLError, OSError, json.JSONDecodeError, KeyError, IndexError) as e:
            log(f"    LLM request error: {e}", C.RED)
            return None, None


def _extract_code(raw: str) -> str:
    """
    Extract Hemlock code from LLM response.
    Handles cases where the model wraps code in markdown fences despite instructions,
    or includes preamble text before the actual code.
    """
    raw = raw.strip()

    if not raw:
        return ""

    # Try to extract from ```hemlock ... ``` or ``` ... ```
    patterns = [
        r"```hemlock\s*\n(.*?)```",
        r"```hml\s*\n(.*?)```",
        r"```\s*\n(.*?)```",
    ]
    for pat in patterns:
        m = re.search(pat, raw, re.DOTALL)
        if m:
            return m.group(1).strip()

    # If it starts with ``` strip that
    if raw.startswith("```"):
        lines = raw.split("\n")
        # Remove first and last ``` lines
        if lines[-1].strip() == "```":
            lines = lines[1:-1]
        else:
            lines = lines[1:]
        return "\n".join(lines).strip()

    # If the response has a mix of prose and code, try to find where code starts.
    # Look for common Hemlock patterns: let, fn, import, print, for, if, etc.
    # Only do this if the response doesn't look like pure code already.
    first_line = raw.split("\n")[0]
    if not _looks_like_code(first_line):
        # Find the first line that looks like Hemlock code
        lines = raw.split("\n")
        for i, line in enumerate(lines):
            if _looks_like_code(line):
                extracted = "\n".join(lines[i:]).strip()
                # Remove any trailing prose after the code
                code_lines = []
                for cl in extracted.split("\n"):
                    if cl.strip() and not _looks_like_code(cl) and not cl.strip().startswith("//") and not cl.strip().startswith("}") and not cl.strip().startswith(")") and len(code_lines) > 3:
                        # Likely hit trailing prose
                        break
                    code_lines.append(cl)
                return "\n".join(code_lines).strip()

    return raw


def _looks_like_code(line: str) -> bool:
    """Heuristic: does this line look like Hemlock source code?"""
    s = line.strip()
    if not s:
        return True  # blank lines are fine in code
    code_starts = [
        "let ", "fn ", "import ", "print(", "eprint(", "for ", "if ",
        "while ", "loop ", "switch ", "match ", "define ", "enum ",
        "async ", "defer ", "try ", "return ", "throw ", "//", "/*",
        "}", "{", "spawn(", "channel(", "alloc(", "buffer(", "free(",
        "type ", "extern ", "signal(", "open(",
    ]
    return any(s.startswith(kw) for kw in code_starts) or s.endswith(";") or s.endswith("{") or s.endswith("}")


# ─── Task Execution ──────────────────────────────────────────────────────────

def run_hemlock(code: str, timeout: int = TASK_TIMEOUT_SEC) -> tuple[str, int, str]:
    """
    Write code to a temp file and run it with the Hemlock interpreter.
    Returns (stdout, exit_code, error_category).
    """
    tmp_file = SCRIPT_DIR / "_hembench_tmp.hml"
    try:
        tmp_file.write_text(code)
        result = subprocess.run(
            [str(HEMLOCK_BIN), str(tmp_file)],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        output = result.stdout
        if result.returncode != 0:
            combined = result.stdout + result.stderr
            if any(kw in combined for kw in ["Parse error", "Syntax error", "Unexpected token"]):
                return combined, result.returncode, "parse_error"
            return combined, result.returncode, "runtime_error"
        return output, 0, "ok"
    except subprocess.TimeoutExpired:
        return "", 124, "timeout"
    except FileNotFoundError:
        return f"Hemlock interpreter not found at {HEMLOCK_BIN}", 1, "missing_interpreter"
    finally:
        tmp_file.unlink(missing_ok=True)


def validate_output(actual: str, expected: str, validator: str, task: dict) -> bool:
    """Compare actual output against expected using the task's validator."""
    actual_trimmed = actual.rstrip()
    expected_trimmed = expected.rstrip()

    if validator == "exact_match":
        return actual_trimmed == expected_trimmed

    if validator == "starts_with":
        prefix = task.get("validator_config", {}).get("required_prefix", "")
        return actual_trimmed.startswith(prefix)

    # Default fallback
    return actual_trimmed == expected_trimmed


# ─── Result Structures ───────────────────────────────────────────────────────

def compute_scores(results: list) -> dict:
    """Compute per-level and weighted overall scores."""
    levels = {}
    for r in results:
        level = r["level"]
        if level not in levels:
            levels[level] = {"total": 0, "correct": 0, "parse_errors": 0, "runtime_errors": 0, "timeouts": 0}
        levels[level]["total"] += 1
        if r["correct"]:
            levels[level]["correct"] += 1
        cat = r.get("error_category", "ok")
        if cat == "parse_error":
            levels[level]["parse_errors"] += 1
        elif cat == "runtime_error":
            levels[level]["runtime_errors"] += 1
        elif cat == "timeout":
            levels[level]["timeouts"] += 1

    # Per-level scores
    for lv in levels:
        t = levels[lv]["total"]
        levels[lv]["score"] = levels[lv]["correct"] / t if t > 0 else 0.0

    # Weighted overall
    weighted_sum = 0.0
    weight_sum = 0.0
    for lv, data in levels.items():
        w = LEVEL_WEIGHTS.get(lv, 0.10)
        weighted_sum += w * data["score"]
        weight_sum += w

    overall = weighted_sum / weight_sum if weight_sum > 0 else 0.0

    return {
        "levels": levels,
        "overall": round(overall, 4),
        "total": sum(d["total"] for d in levels.values()),
        "correct": sum(d["correct"] for d in levels.values()),
    }


# ─── Main Benchmark Loop ────────────────────────────────────────────────────

def run_benchmark(
    model_path: str,
    server,
    tasks: list,
    variant: str,
    docs: str,
    compact_docs: str,
    output_dir: Path,
    dry_run: bool = False,
    max_tokens: int = MAX_TOKENS,
    temperature: float = TEMPERATURE,
    verbose: bool = False,
) -> dict:
    """Run the full benchmark for one model."""
    model_name = model_short_name(model_path)
    system_prompt = build_system_prompt(variant, docs, compact_docs)
    solutions_dir = output_dir / "solutions"
    solutions_dir.mkdir(parents=True, exist_ok=True)

    results = []

    for i, task in enumerate(tasks, 1):
        tid = task["id"]
        title = task["title"]
        difficulty = task["difficulty"]
        expected = task.get("expected_output", "")
        validator = task.get("validator", "exact_match")
        task_timeout = task.get("timeout_ms", TASK_TIMEOUT_SEC * 1000)
        task_timeout_sec = max((task_timeout + 999) // 1000, 5)

        # Display progress
        progress = f"[{i}/{len(tasks)}]"
        print(f"  {C.DIM}{progress}{C.NC} {C.BOLD}{tid}{C.NC} {title} {C.DIM}({difficulty}){C.NC}", end="", flush=True)

        if dry_run:
            print(f"  {C.YELLOW}○ dry-run{C.NC}")
            results.append({
                "id": tid, "level": task["level"], "title": title,
                "difficulty": difficulty, "status": "dry-run",
                "correct": False, "parses": False, "runs": False,
            })
            continue

        # Generate code via LLM
        user_prompt = build_user_prompt(task)
        gen_start = time.time()
        code, raw_response = server.generate(system_prompt, user_prompt,
                                              max_tokens=max_tokens, temperature=temperature,
                                              verbose=verbose)
        gen_time = time.time() - gen_start

        if code is None:
            print(f"  {C.RED}✗ generation failed{C.NC}  {C.DIM}{gen_time:.1f}s{C.NC}")
            results.append({
                "id": tid, "level": task["level"], "title": title,
                "difficulty": difficulty, "status": "generation_failed",
                "correct": False, "parses": False, "runs": False,
                "gen_time_s": round(gen_time, 2),
            })
            continue

        # Save generated solution + raw response
        sol_file = solutions_dir / f"{tid}_{title.lower().replace(' ', '_')}.hml"
        sol_file.write_text(code)
        raw_file = solutions_dir / f"{tid}_raw.txt"
        raw_file.write_text(raw_response or "")

        if verbose and code:
            log(f"\n    ── extracted code ({len(code)} chars) ──", C.CYAN)
            for line in code.split("\n")[:20]:
                log(f"    │ {line}", C.CYAN)
            if len(code.split("\n")) > 20:
                log(f"    │ ... ({len(code.split(chr(10))) - 20} more lines)", C.CYAN)
            log(f"    ── end extracted ──", C.CYAN)
        elif verbose:
            log(f"\n    ── WARNING: extracted code is empty! ──", C.YELLOW)

        # Run through interpreter
        actual, exit_code, error_cat = run_hemlock(code, timeout=task_timeout_sec)

        parses = error_cat != "parse_error"
        runs = error_cat in ("ok", "runtime_error") and exit_code != 139
        correct = False

        if exit_code == 0:
            correct = validate_output(actual, expected, validator, task)

        # Determine status
        if correct:
            status = "pass"
            icon, color = "✓", C.GREEN
        elif error_cat == "timeout":
            status = "timeout"
            icon, color = "⏱", C.YELLOW
        elif error_cat == "parse_error":
            status = "parse_error"
            icon, color = "✗", C.RED
        elif exit_code == 139:
            status = "segfault"
            icon, color = "✗", C.RED
        elif exit_code != 0:
            status = "runtime_error"
            icon, color = "✗", C.RED
        else:
            status = "wrong_output"
            icon, color = "✗", C.YELLOW

        print(f"  {color}{icon} {status}{C.NC}  {C.DIM}{gen_time:.1f}s gen{C.NC}")

        # Show mismatch details
        if not correct and runs and exit_code == 0:
            exp_first = expected.strip().split("\n")[0] if expected else "(empty)"
            act_first = actual.strip().split("\n")[0] if actual else "(empty)"
            print(f"           {C.DIM}expected: {exp_first}{C.NC}")
            print(f"           {C.DIM}got:      {act_first}{C.NC}")
        elif not correct and not parses:
            err_line = actual.strip().split("\n")[0] if actual else "(no output)"
            print(f"           {C.DIM}error: {err_line}{C.NC}")

        result = {
            "id": tid,
            "level": task["level"],
            "title": title,
            "difficulty": difficulty,
            "status": status,
            "correct": correct,
            "parses": parses,
            "runs": runs,
            "exit_code": exit_code,
            "error_category": error_cat,
            "gen_time_s": round(gen_time, 2),
            "lines": len(code.strip().split("\n")),
            "solution_file": str(sol_file.name),
        }

        # Include output diff for debugging
        if not correct and actual:
            result["actual_output_head"] = actual.strip()[:500]

        results.append(result)

    return {
        "model": model_name,
        "model_path": model_path,
        "variant": variant,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "results": results,
        "scores": compute_scores(results),
    }


def print_summary(report: dict):
    """Print a pretty summary of benchmark results."""
    scores = report["scores"]
    model = report["model"]

    print()
    print(f"{C.BOLD}{'═' * 60}{C.NC}")
    print(f"{C.BOLD}  Results: {model}{C.NC}")
    print(f"{C.BOLD}{'═' * 60}{C.NC}")
    print(f"  Variant:  {report['variant']}")
    print(f"  Total:    {scores['total']} tasks")
    print(f"  {C.GREEN}Correct:{C.NC}  {scores['correct']}")
    print(f"  {C.RED}Failed:{C.NC}   {scores['total'] - scores['correct']}")
    print()

    # Per-level breakdown
    print(f"  {C.BOLD}Per-Level Scores:{C.NC}")
    for lv in sorted(scores["levels"].keys()):
        data = scores["levels"][lv]
        pct = data["score"] * 100
        bar_len = int(pct / 5)
        bar = "█" * bar_len + "░" * (20 - bar_len)
        weight = LEVEL_WEIGHTS.get(lv, 0)

        color = C.GREEN if pct >= 70 else C.YELLOW if pct >= 40 else C.RED
        print(f"    {lv}  {bar}  {color}{pct:5.1f}%{C.NC}  ({data['correct']}/{data['total']})  {C.DIM}weight: {weight}{C.NC}")

        # Show error breakdown if any
        extras = []
        if data.get("parse_errors"):
            extras.append(f"{data['parse_errors']} parse err")
        if data.get("runtime_errors"):
            extras.append(f"{data['runtime_errors']} runtime err")
        if data.get("timeouts"):
            extras.append(f"{data['timeouts']} timeout")
        if extras:
            print(f"         {C.DIM}{', '.join(extras)}{C.NC}")

    print()
    overall_pct = scores["overall"] * 100
    color = C.GREEN if overall_pct >= 70 else C.YELLOW if overall_pct >= 40 else C.RED
    print(f"  {C.BOLD}Weighted Overall: {color}{overall_pct:.1f}%{C.NC}")
    print(f"{C.BOLD}{'═' * 60}{C.NC}")


def print_comparison(reports: list):
    """Print a side-by-side comparison table of multiple models."""
    if len(reports) < 2:
        return

    print()
    print(f"{C.BOLD}{'═' * 70}{C.NC}")
    print(f"{C.BOLD}  Model Comparison{C.NC}")
    print(f"{C.BOLD}{'═' * 70}{C.NC}")

    # Header
    names = [r["model"][:25] for r in reports]
    header = f"  {'Level':<8}" + "".join(f"{n:>28}" for n in names)
    print(header)
    print(f"  {'─' * (8 + 28 * len(names))}")

    all_levels = sorted(set(lv for r in reports for lv in r["scores"]["levels"]))

    for lv in all_levels:
        row = f"  {lv:<8}"
        for r in reports:
            data = r["scores"]["levels"].get(lv, {"score": 0, "correct": 0, "total": 0})
            pct = data["score"] * 100
            color = C.GREEN if pct >= 70 else C.YELLOW if pct >= 40 else C.RED
            cell = f"{color}{pct:5.1f}%{C.NC} ({data['correct']}/{data['total']})"
            row += f"{cell:>40}"  # extra width for ANSI codes
        print(row)

    # Overall
    print(f"  {'─' * (8 + 28 * len(names))}")
    row = f"  {'OVERALL':<8}"
    for r in reports:
        pct = r["scores"]["overall"] * 100
        color = C.GREEN if pct >= 70 else C.YELLOW if pct >= 40 else C.RED
        cell = f"{C.BOLD}{color}{pct:5.1f}%{C.NC}"
        row += f"{cell:>40}"
    print(row)
    print(f"{C.BOLD}{'═' * 70}{C.NC}")


# ─── CLI Entry Point ─────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="HemBench LLM — Benchmark LLMs on Hemlock code generation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --model ~/AI/Models/model.gguf
  %(prog)s --model-dir ~/AI/Models/
  %(prog)s --model model.gguf --level L1 --variant zero-shot
  %(prog)s --model model.gguf --dry-run
        """,
    )

    # Model selection (one or many)
    model_group = parser.add_mutually_exclusive_group(required=True)
    model_group.add_argument("--model", type=str, help="Path to a single .gguf model file")
    model_group.add_argument("--model-dir", type=str, help="Directory of .gguf files to benchmark")

    # Server config
    parser.add_argument("--llama-server", type=str, default="llama-server",
                        help="Path to llama-server binary (default: llama-server)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Port for llama-server (default: {DEFAULT_PORT})")
    parser.add_argument("--ctx-size", type=int, default=32768,
                        help="Context size for llama-server (default: 32768)")
    parser.add_argument("--n-gpu-layers", type=int, default=-1,
                        help="GPU layers (-1 = all, 0 = CPU only)")

    # Task filtering
    parser.add_argument("--level", type=str, default="",
                        help="Run only tasks for a specific level (L1-L6)")
    parser.add_argument("--task", type=str, default="",
                        help="Run a single task by ID (e.g. L1-E-01)")

    # Prompt config
    parser.add_argument("--variant", type=str, default="zero-shot",
                        choices=["zero-shot", "doc-guided", "few-shot", "compact-doc"],
                        help="Prompt variant (default: zero-shot, evaluates raw model knowledge)")

    # Generation config
    parser.add_argument("--max-tokens", type=int, default=MAX_TOKENS,
                        help=f"Max tokens for LLM generation (default: {MAX_TOKENS})")
    parser.add_argument("--temperature", type=float, default=TEMPERATURE,
                        help=f"Sampling temperature (default: {TEMPERATURE})")

    # Output
    parser.add_argument("--output", type=str, default="",
                        help="Output directory for results (default: benchmark/results/<model>/)")
    parser.add_argument("--json", action="store_true",
                        help="Also write JSON results file")

    # Misc
    parser.add_argument("--dry-run", action="store_true",
                        help="Don't call LLM; just list tasks that would be run")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show raw LLM responses and generated code for debugging")
    parser.add_argument("--retries", type=int, default=1,
                        help="Number of attempts per task on generation failure (default: 1)")

    args = parser.parse_args()

    # Update generation config from args
    max_tokens = args.max_tokens
    temperature = args.temperature

    # Collect models
    if args.model:
        model_paths = [os.path.expanduser(args.model)]
    else:
        model_dir = os.path.expanduser(args.model_dir)
        model_paths = sorted(glob.glob(os.path.join(model_dir, "*.gguf")))
        if not model_paths:
            log(f"No .gguf files found in {model_dir}", C.RED)
            sys.exit(1)

    # Verify hemlock interpreter
    if not HEMLOCK_BIN.exists():
        log(f"Hemlock interpreter not found at {HEMLOCK_BIN}", C.RED)
        log("Build it with: make", C.DIM)
        sys.exit(1)

    # Load tasks
    tasks = load_tasks(args.level, args.task)
    if not tasks:
        log("No tasks found matching filters.", C.RED)
        sys.exit(1)

    # Load docs
    docs, compact_docs = load_hemlock_docs()

    # Banner
    print()
    print(f"{C.BOLD}╔══════════════════════════════════════════════════════════╗{C.NC}")
    print(f"{C.BOLD}║       HemBench LLM — Hemlock Code Generation Benchmark  ║{C.NC}")
    print(f"{C.BOLD}╚══════════════════════════════════════════════════════════╝{C.NC}")
    print()
    print(f"  {C.CYAN}Models:{C.NC}    {len(model_paths)}")
    print(f"  {C.CYAN}Tasks:{C.NC}     {len(tasks)} ({args.level or 'all levels'})")
    print(f"  {C.CYAN}Variant:{C.NC}   {args.variant}")
    print(f"  {C.CYAN}Max tokens:{C.NC} {max_tokens}")
    print(f"  {C.CYAN}Temp:{C.NC}      {temperature}")
    print()

    all_reports = []

    for model_idx, model_path in enumerate(model_paths, 1):
        model_name = model_short_name(model_path)

        if not os.path.isfile(model_path):
            log(f"Model file not found: {model_path}", C.RED)
            continue

        if len(model_paths) > 1:
            print(f"{C.BOLD}{C.MAGENTA}━━━ Model {model_idx}/{len(model_paths)}: {model_name} ━━━{C.NC}")
            print()

        # Determine output directory
        if args.output:
            output_dir = Path(args.output) / model_name
        else:
            output_dir = SCRIPT_DIR / "results" / model_name
        output_dir.mkdir(parents=True, exist_ok=True)

        server = None
        try:
            if not args.dry_run:
                server = LlamaServer(
                    model_path=model_path,
                    llama_bin=args.llama_server,
                    port=args.port,
                    n_ctx=args.ctx_size,
                    n_gpu_layers=args.n_gpu_layers,
                )
                if not server.start():
                    log(f"Failed to start server for {model_name}, skipping.", C.RED)
                    continue

            # Run benchmark
            report = run_benchmark(
                model_path=model_path,
                server=server,
                tasks=tasks,
                variant=args.variant,
                docs=docs,
                compact_docs=compact_docs,
                output_dir=output_dir,
                dry_run=args.dry_run,
                max_tokens=max_tokens,
                temperature=temperature,
                verbose=args.verbose,
            )

            all_reports.append(report)
            print_summary(report)

            # Save JSON results
            if args.json or True:  # Always save JSON
                json_file = output_dir / "results.json"
                with open(json_file, "w") as f:
                    json.dump(report, f, indent=2)
                log(f"  Results saved: {json_file}", C.DIM)

        finally:
            if server:
                log("  Stopping llama-server...", C.DIM)
                server.stop()

        print()

    # Comparison table if multiple models
    if len(all_reports) > 1:
        print_comparison(all_reports)

        # Save combined report
        combined_file = SCRIPT_DIR / "results" / "comparison.json"
        combined_file.parent.mkdir(parents=True, exist_ok=True)
        with open(combined_file, "w") as f:
            json.dump({
                "benchmark": "HemBench LLM",
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "models": [r["model"] for r in all_reports],
                "reports": all_reports,
            }, f, indent=2)
        log(f"Comparison saved: {combined_file}", C.DIM)


if __name__ == "__main__":
    main()
