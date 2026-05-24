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


def load_tasks(level_filter: str = "", task_filter: str = ""):
    """Load all benchmark task JSON files, optionally filtered."""
    level_dirs = [
        level_dir
        for level_dir in sorted(TASKS_DIR.iterdir())
        if level_dir.is_dir()
    ]

    if level_filter:
        level_dirs = [
            level_dir
            for level_dir in level_dirs
            if level_dir.name.split("_")[0] == level_filter
        ]

    tasks = []
    for level_dir in level_dirs:
        for task_file in sorted(level_dir.glob("*.json")):
            with open(task_file) as f:
                task = json.load(f)
            task["_file"] = str(task_file)
            tasks.append(task)

    if task_filter:
        tasks = [task for task in tasks if task["id"] == task_filter]

    return tasks


# ─── Prompt Construction ─────────────────────────────────────────────────────

def build_system_prompt(variant: str, docs: str, compact_docs: str, base_override: str = None) -> str:
    """Build the system prompt based on variant. If base_override is provided, use it instead of the default base."""
    if base_override is not None:
        base = base_override.rstrip() + "\n"
    else:
        base = (
            "You are a code generation assistant. You write programs in the Hemlock programming language.\n"
            "IMPORTANT: Respond ONLY with the Hemlock source code. No markdown fences, no explanations, "
            "no comments about the code — just the raw .hml program that can be run directly.\n"
            "Do NOT wrap your code in ```hemlock``` or ``` blocks.\n"
        )

    if variant == "zero-shot":
        return base.rstrip() + "\n"

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
    examples = [
        f"// Example: {sol_file.stem}\n{sol_file.read_text()}"
        for sol_file in sorted(solutions_dir.glob("*.hml"))[:3]
    ]
    return "\n\n".join(examples) if examples else "(no examples available)"


def build_retry_feedback(status: str, exit_code: int, actual: str, expected: str, validator: str) -> str:
    """Build a feedback message for retry-with-feedback mode after a failed attempt."""
    actual = (actual or "").strip()
    expected = (expected or "").strip()

    if status == "parse_error":
        err = actual.split("\n")[0] if actual else "(no output)"
        return (f"That code did not parse. The Hemlock interpreter reported:\n\n{actual[:1500]}\n\n"
                "Please fix the syntax and produce a corrected program. Respond ONLY with the corrected Hemlock code.")
    if status == "runtime_error" or status == "segfault":
        return (f"That code compiled but failed at runtime (exit code {exit_code}). Interpreter output:\n\n{actual[:1500]}\n\n"
                "Please fix the bug and produce a corrected program. Respond ONLY with the corrected Hemlock code.")
    if status == "timeout":
        return ("That code timed out. It likely has an infinite loop or is too slow. "
                "Please fix it and produce a corrected program. Respond ONLY with the corrected Hemlock code.")
    if status == "wrong_output":
        return (f"The code ran successfully but produced the wrong output.\n\n"
                f"Expected output:\n{expected[:800]}\n\n"
                f"Your program's output:\n{actual[:800]}\n\n"
                "Please fix the logic so the output matches exactly. Respond ONLY with the corrected Hemlock code.")
    return ("That attempt was incorrect. Please try again. Respond ONLY with the corrected Hemlock code.")


def build_user_prompt(task: dict) -> str:
    """Build the user-facing prompt for a single task."""
    prompt = task["prompt"]

    # For debugging tasks, include the buggy code prominently
    if "buggy_code" in task:
        prompt += f"\n\nBuggy code:\n{task['buggy_code']}"

    # For translation tasks, source is usually embedded in the prompt already
    # Add expected output hint — but NOT for debugging (L6) or translation (L5) tasks,
    # where giving away the answer undermines what we're measuring
    level = task.get("level", "")
    if task.get("expected_output") and level not in ("L5", "L6"):
        prompt += f"\n\nThe program should produce this exact output:\n{task['expected_output'].rstrip()}"

    return prompt


# ─── LLM Server Management ──────────────────────────────────────────────────

class LlamaServer:
    """Manages a llama-server subprocess lifecycle."""

    def __init__(self, model_path: str, llama_bin: str = "llama-server",
                 port: int = DEFAULT_PORT, n_ctx: int = 8192, n_gpu_layers: int = -1,
                 llm_timeout: int = LLM_TIMEOUT_SEC):
        self.model_path = model_path
        self.llama_bin = llama_bin
        self.port = port
        self.n_ctx = n_ctx
        self.n_gpu_layers = n_gpu_layers
        self.llm_timeout = llm_timeout
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
        return self.generate_messages(
            [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            max_tokens=max_tokens, temperature=temperature, verbose=verbose,
        )

    def generate_messages(self, messages: list,
                          max_tokens: int = MAX_TOKENS, temperature: float = TEMPERATURE,
                          verbose: bool = False):
        """Send a chat completion request with a full messages array."""
        payload = {
            "model": "local",
            "messages": messages,
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
            with urllib.request.urlopen(req, timeout=self.llm_timeout) as resp:
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
    system_prompt_override: str = None,
    n_samples: int = 1,
    retry_with_feedback: int = 0,
    hybrid_retry: int = 0,
    adaptive_retry: int = 0,
) -> dict:
    """Run the full benchmark for one model."""
    model_name = model_short_name(model_path)
    system_prompt = build_system_prompt(variant, docs, compact_docs, base_override=system_prompt_override)
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

        # Generate samples via LLM, stopping early if one passes.
        # Three modes:
        #   n_samples           — independent fresh attempts (pass@K)
        #   retry_with_feedback — multi-turn conversation; feed back errors
        #   hybrid_retry        — attempt 1 cold, attempt 2 feedback,
        #                         attempts 3+ cold resample at higher temp
        user_prompt = build_user_prompt(task)
        samples = []
        any_correct = False

        use_adaptive = adaptive_retry > 0
        use_hybrid = hybrid_retry > 0 and not use_adaptive
        use_retry = retry_with_feedback > 0 and not use_hybrid and not use_adaptive
        if use_adaptive:
            total_attempts = adaptive_retry
        elif use_hybrid:
            total_attempts = hybrid_retry
        elif use_retry:
            total_attempts = retry_with_feedback
        else:
            total_attempts = n_samples
        if use_adaptive or use_hybrid or use_retry:
            sample_temp_default = temperature
        elif total_attempts == 1:
            sample_temp_default = temperature
        else:
            sample_temp_default = max(temperature, 0.7)

        # Conversation state (only used in retry / hybrid-feedback turns)
        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ]

        def hybrid_strategy(idx):
            """Match hembot's src/strategy.hml:
            idx=0 -> cold (initial), idx=1 -> feedback, idx>=2 -> cold resample."""
            if idx == 1:
                return ("feedback", temperature)
            return ("cold", temperature if idx == 0 else max(temperature, 0.7))

        def adaptive_strategy(idx, prev_samples):
            """Pick per-attempt strategy from the last failure's category.
            Attempt 0 is always cold at base temp. Thereafter, last status:
              wrong_output → feedback (retry helps on close-to-right bugs)
              anything else → cold resample at higher temp
            """
            if idx == 0 or not prev_samples:
                return ("cold", temperature)
            last_status = prev_samples[-1].get("status", "")
            if last_status == "wrong_output":
                return ("feedback", temperature)
            return ("cold", max(temperature, 0.7))

        for sample_idx in range(total_attempts):
            sample_temp = sample_temp_default
            hybrid_mode = None

            gen_start = time.time()
            if use_retry:
                code, raw_response = server.generate_messages(
                    messages, max_tokens=max_tokens, temperature=sample_temp, verbose=verbose)
            elif use_hybrid or use_adaptive:
                if use_adaptive:
                    hybrid_mode, sample_temp = adaptive_strategy(sample_idx, samples)
                else:
                    hybrid_mode, sample_temp = hybrid_strategy(sample_idx)
                if hybrid_mode == "feedback":
                    code, raw_response = server.generate_messages(
                        messages, max_tokens=max_tokens, temperature=sample_temp, verbose=verbose)
                else:
                    # Cold resample: fresh [system, user], don't pollute `messages`.
                    fresh = [
                        {"role": "system", "content": system_prompt},
                        {"role": "user", "content": user_prompt},
                    ]
                    code, raw_response = server.generate_messages(
                        fresh, max_tokens=max_tokens, temperature=sample_temp, verbose=verbose)
            else:
                code, raw_response = server.generate(system_prompt, user_prompt,
                                                      max_tokens=max_tokens, temperature=sample_temp,
                                                      verbose=verbose)
            gen_time = time.time() - gen_start

            if code is None:
                samples.append({
                    "sample_idx": sample_idx,
                    "status": "generation_failed",
                    "correct": False, "parses": False, "runs": False,
                    "gen_time_s": round(gen_time, 2),
                    "exit_code": -1, "error_category": "generation_failed",
                    "lines": 0,
                })
                break  # No point retrying if the request itself failed

            # Save generated solution + raw response (suffix with sample idx if multi-attempt)
            suffix = f"_s{sample_idx}" if total_attempts > 1 else ""
            sol_file = solutions_dir / f"{tid}_{title.lower().replace(' ', '_')}{suffix}.hml"
            sol_file.write_text(code)
            raw_file = solutions_dir / f"{tid}_raw{suffix}.txt"
            raw_file.write_text(raw_response or "")

            # Run through interpreter
            actual, exit_code, error_cat = run_hemlock(code, timeout=task_timeout_sec)

            parses = error_cat != "parse_error"
            runs = error_cat in ("ok", "runtime_error") and exit_code != 139
            correct = False
            if exit_code == 0:
                correct = validate_output(actual, expected, validator, task)

            if correct:
                status = "pass"
            elif error_cat == "timeout":
                status = "timeout"
            elif error_cat == "parse_error":
                status = "parse_error"
            elif exit_code == 139:
                status = "segfault"
            elif exit_code != 0:
                status = "runtime_error"
            else:
                status = "wrong_output"

            samples.append({
                "sample_idx": sample_idx,
                "status": status,
                "correct": correct,
                "parses": parses,
                "runs": runs,
                "exit_code": exit_code,
                "error_category": error_cat,
                "gen_time_s": round(gen_time, 2),
                "lines": len(code.strip().split("\n")),
                "actual_output_head": (actual.strip()[:500] if not correct else ""),
            })

            if correct:
                any_correct = True
                break  # Early exit on first pass

            # Feedback bookkeeping: append the failing code + error to the
            # conversation so the NEXT turn (if it's a feedback turn) sees it.
            if sample_idx + 1 < total_attempts:
                if use_retry:
                    next_is_feedback = True
                elif use_hybrid:
                    next_is_feedback = hybrid_strategy(sample_idx + 1)[0] == "feedback"
                elif use_adaptive:
                    # Adaptive looks at the just-recorded sample to decide.
                    next_is_feedback = adaptive_strategy(sample_idx + 1, samples)[0] == "feedback"
                else:
                    next_is_feedback = False
                if next_is_feedback:
                    feedback = build_retry_feedback(status, exit_code, actual, expected, validator)
                    messages.append({"role": "assistant", "content": raw_response or ""})
                    messages.append({"role": "user", "content": feedback})

        # Aggregate across samples
        first_sample = samples[0]
        total_gen_time = sum(s.get("gen_time_s", 0) for s in samples)
        num_tried = len(samples)

        # pass@1 = first sample correct; pass@K = any correct
        pass_at_1 = first_sample.get("correct", False)
        pass_at_k = any_correct

        # Report using the pass@K result for display
        final_status = "pass" if pass_at_k else (samples[-1]["status"] if samples else "generation_failed")
        final_correct = pass_at_k

        # Choose color/icon
        if pass_at_k:
            icon, color = "✓", C.GREEN
            if n_samples > 1 and not pass_at_1:
                # Passed on retry
                icon = f"✓@{num_tried}"
                color = C.CYAN
        elif final_status == "timeout":
            icon, color = "⏱", C.YELLOW
        elif final_status in ("parse_error", "segfault", "runtime_error"):
            icon, color = "✗", C.RED
        else:
            icon, color = "✗", C.YELLOW

        progress_note = f" (×{num_tried})" if n_samples > 1 else ""
        print(f"  {color}{icon} {final_status}{C.NC}{progress_note}  {C.DIM}{total_gen_time:.1f}s gen{C.NC}")

        # Show details from the LAST sample for context
        if not final_correct:
            last = samples[-1]
            if last.get("runs") and last.get("exit_code") == 0:
                exp_first = expected.strip().split("\n")[0] if expected else "(empty)"
                act_first = last.get("actual_output_head", "").split("\n")[0] if last.get("actual_output_head") else "(empty)"
                print(f"           {C.DIM}expected: {exp_first}{C.NC}")
                print(f"           {C.DIM}got:      {act_first}{C.NC}")
            elif not last.get("parses"):
                err_line = last.get("actual_output_head", "").split("\n")[0] if last.get("actual_output_head") else "(no output)"
                print(f"           {C.DIM}error: {err_line}{C.NC}")

        result = {
            "id": tid,
            "level": task["level"],
            "title": title,
            "difficulty": difficulty,
            "status": final_status,
            "correct": final_correct,
            "pass_at_1": pass_at_1,
            "pass_at_k": pass_at_k,
            "n_samples_tried": num_tried,
            "n_samples_requested": total_attempts,
            "mode": (
                "adaptive" if use_adaptive
                else "hybrid" if use_hybrid
                else "retry" if use_retry
                else "samples" if n_samples > 1
                else "single"
            ),
            "parses": samples[-1].get("parses", False),
            "runs": samples[-1].get("runs", False),
            "exit_code": samples[-1].get("exit_code", -1),
            "error_category": samples[-1].get("error_category", "unknown"),
            "gen_time_s": round(total_gen_time, 2),
            "samples": samples,
            "lines": samples[-1].get("lines", 0),
        }

        results.append(result)

    return {
        "model": model_name,
        "model_path": model_path,
        "variant": variant,
        "mode": (
            "adaptive" if adaptive_retry > 0
            else "hybrid" if hybrid_retry > 0
            else "retry" if retry_with_feedback > 0
            else "samples" if n_samples > 1
            else "single"
        ),
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

    # If multi-sample run, also show pass@1 vs pass@K comparison
    rows = report.get("results", [])
    if rows and any(r.get("n_samples_requested", 1) > 1 for r in rows):
        k = max(r.get("n_samples_requested", 1) for r in rows)
        pass1 = sum(1 for r in rows if r.get("pass_at_1")) / len(rows)
        passk = sum(1 for r in rows if r.get("pass_at_k")) / len(rows)
        gap = passk - pass1
        mode = report.get("mode", "samples")
        if mode == "retry":
            label = f"retry@{k}"
        elif mode == "hybrid":
            label = f"hybrid@{k}"
        elif mode == "adaptive":
            label = f"adaptive@{k}"
        else:
            label = f"pass@{k}"
        print(f"  {C.BOLD}pass@1:{C.NC} {pass1*100:5.1f}%  {C.BOLD}{label}:{C.NC} {passk*100:5.1f}%  {C.DIM}(gap: +{gap*100:.1f} pts){C.NC}")
        # Per-attempt breakdown: at which attempt did tasks pass?
        if mode == "retry":
            turn_dist = {}
            for r in rows:
                if r.get("pass_at_k"):
                    samples = r.get("samples", [])
                    for s in samples:
                        if s.get("correct"):
                            t = s.get("sample_idx", 0) + 1
                            turn_dist[t] = turn_dist.get(t, 0) + 1
                            break
            if turn_dist:
                parts = [f"turn {t}: {n}" for t, n in sorted(turn_dist.items())]
                print(f"  {C.DIM}Passes by turn: {', '.join(parts)}{C.NC}")

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
    parser.add_argument("--system-prompt-file", type=str, default=None,
                        help="Path to a file containing a custom system prompt. Overrides --variant's base prompt "
                             "but still appends doc/compact-doc/few-shot content if variant is set.")
    parser.add_argument("--n-samples", type=int, default=1,
                        help="Number of attempts per task. Pass@K eval: task passes if any attempt succeeds. "
                             "Temperature auto-bumped to 0.7+ when >1 for diversity. (default: 1)")
    parser.add_argument("--retry-with-feedback", type=int, default=0, metavar="K",
                        help="Agent retry mode: if the first attempt fails, feed back the error and give the "
                             "model up to K-1 more turns to fix its code. Measures self-correction ability. "
                             "Mutually exclusive with --n-samples. (default: 0, disabled)")
    parser.add_argument("--hybrid-retry", type=int, default=0, metavar="K",
                        help="Hybrid retry mode: attempt 1 = cold (temp=base), attempt 2 = feedback (temp=base), "
                             "attempt 3+ = cold resample (temp=0.7, fresh conversation). Empirically between "
                             "pure retry and pure pass@K on Apothecary. Mutually exclusive with the others. "
                             "(default: 0, disabled)")
    parser.add_argument("--adaptive-retry", type=int, default=0, metavar="K",
                        help="Adaptive retry: branch on the last failure's error category. "
                             "wrong_output → feedback (close-to-right, feedback fixes it). "
                             "parse/runtime/segfault/timeout → cold resample (structural, new try). "
                             "Mutually exclusive with the other retry modes. (default: 0, disabled)")

    # Generation config
    parser.add_argument("--max-tokens", type=int, default=MAX_TOKENS,
                        help=f"Max tokens for LLM generation (default: {MAX_TOKENS})")
    parser.add_argument("--temperature", type=float, default=TEMPERATURE,
                        help=f"Sampling temperature (default: {TEMPERATURE})")
    parser.add_argument("--llm-timeout", type=int, default=LLM_TIMEOUT_SEC,
                        help=f"Timeout in seconds per LLM request (default: {LLM_TIMEOUT_SEC})")

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

    # Load custom system prompt if provided
    system_prompt_override = None
    if args.system_prompt_file:
        sp_path = Path(args.system_prompt_file)
        if not sp_path.exists():
            log(f"System prompt file not found: {sp_path}", C.RED)
            sys.exit(1)
        system_prompt_override = sp_path.read_text(encoding="utf-8")
        log(f"Using custom system prompt from {sp_path} ({len(system_prompt_override)} chars)", C.CYAN)

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
                    llm_timeout=args.llm_timeout,
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
                system_prompt_override=system_prompt_override,
                n_samples=args.n_samples,
                retry_with_feedback=args.retry_with_feedback,
                hybrid_retry=args.hybrid_retry,
                adaptive_retry=args.adaptive_retry,
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
