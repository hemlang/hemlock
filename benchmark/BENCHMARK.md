# HemBench: LLM Benchmark for Hemlock Code Generation

## Motivation

Hemlock is a novel systems scripting language with no meaningful presence in LLM training corpora. This makes it an ideal benchmark target: models cannot rely on memorized solutions and must instead demonstrate genuine language understanding, generalization from documentation, and compositional reasoning.

HemBench measures an LLM's ability to:
1. Learn a new language from documentation alone
2. Generate syntactically and semantically correct code
3. Combine language features compositionally
4. Reason about low-level concerns (memory, concurrency, types)
5. Translate idioms across languages

---

## Benchmark Structure

### Levels (L1-L6)

Each level tests a different dimension of capability, with increasing difficulty.

| Level | Name | What It Tests | Scoring |
|-------|------|---------------|---------|
| **L1** | Syntax & Basics | Can the LLM write valid Hemlock at all? | Pass/fail via interpreter |
| **L2** | Stdlib Usage | Can it use stdlib modules correctly? | Output match |
| **L3** | Algorithms | Can it implement algorithms in Hemlock? | Output match + correctness |
| **L4** | Systems Programming | Memory management, concurrency, FFI | Output match + no leaks |
| **L5** | Translation | Convert code from other languages to idiomatic Hemlock | Output match + idiom score |
| **L6** | Debugging | Find and fix bugs in Hemlock code | Diff against fix + output match |

### Difficulty Tiers Within Each Level

Each level contains tasks at three difficulty tiers:

- **Easy (E)**: Single concept, <20 lines expected
- **Medium (M)**: Multiple concepts combined, 20-60 lines
- **Hard (H)**: Complex composition, 60+ lines, edge cases matter

---

## Level Details

### L1: Syntax & Basics

Tests whether the LLM can produce syntactically valid Hemlock that runs without errors and produces correct output.

**Topics covered:**
- Variable declarations with type annotations
- Arithmetic with type promotion (i32 + f64 -> f64)
- Control flow (if/else, while, for, for-in, loop, switch)
- Functions (closures, default params, expression-bodied)
- Pattern matching (literals, guards, destructuring)
- String/array methods
- Object literals and `define` blocks
- Null coalescing (`??`, `?.`, `??=`)
- Named arguments

**Example task (L1-E-01):**
```
Prompt: Write a Hemlock function `fizzbuzz(n)` that prints numbers 1 to n,
replacing multiples of 3 with "Fizz", multiples of 5 with "Buzz", and
multiples of both with "FizzBuzz".
```

**Example task (L1-H-03):**
```
Prompt: Write a Hemlock program using pattern matching to evaluate a simple
arithmetic expression tree. Define expressions as objects with a `type` field
("num", "add", "mul") and evaluate recursively. Support nested expressions
like (2 + 3) * (4 + 1).
```

### L2: Standard Library Usage

Tests ability to import and correctly use Hemlock's 42 stdlib modules.

**Topics covered:**
- `@stdlib/collections` (HashMap, Set, Queue, Stack)
- `@stdlib/math` (trig, rounding, constants)
- `@stdlib/json` (parse, stringify)
- `@stdlib/fs` (read_file, write_file)
- `@stdlib/datetime` (DateTime, formatting)
- `@stdlib/encoding` (base64, hex)
- `@stdlib/regex` (compile, test)
- `@stdlib/crypto` (hashing, random_bytes)
- Combining multiple stdlib modules

**Example task (L2-M-02):**
```
Prompt: Write a Hemlock program that reads a JSON file containing an array of
{name, score} objects, filters to scores above 80, sorts by score descending,
and writes the result as a new JSON file. Use @stdlib/fs and @stdlib/json.
```

### L3: Algorithms & Data Structures

Tests implementation of classic algorithms using Hemlock's features.

**Topics covered:**
- Sorting algorithms (using Hemlock arrays and closures)
- Graph algorithms (BFS, DFS using collections)
- Dynamic programming
- String manipulation algorithms
- Tree operations using objects
- Recursive and iterative approaches

**Example task (L3-M-01):**
```
Prompt: Implement a binary search tree in Hemlock using objects. Support
insert, search, and in-order traversal. The traversal should return a
sorted array. Use define blocks for the node structure.
```

**Example task (L3-H-02):**
```
Prompt: Implement Dijkstra's shortest path algorithm in Hemlock. Use
@stdlib/collections HashMap for the adjacency list representation.
The function should take a graph (as an object mapping node names to
arrays of {to, weight} objects) and a start node, returning an object
mapping each node to its shortest distance.
```

### L4: Systems Programming

Tests Hemlock's distinguishing features: manual memory, concurrency, FFI.

**Topics covered:**
- `alloc`/`free` with pointer operations
- `buffer` with bounds-checked access
- `defer` for cleanup
- Async/await with `spawn`/`join`
- Channels for inter-task communication
- Atomic operations for lock-free data structures
- FFI declarations and calls
- Signal handling

**Example task (L4-M-01):**
```
Prompt: Write a Hemlock program that allocates a buffer of 256 bytes,
writes the ASCII values 0-255 into it using ptr_write_u8, then reads
them back and verifies each value. Use defer to ensure free() is called.
Print "PASS" if all values match, "FAIL: index N" for the first mismatch.
```

**Example task (L4-H-01):**
```
Prompt: Implement a producer-consumer pattern in Hemlock. Spawn 3 producer
tasks that each send 10 numbers to a shared channel, and 2 consumer tasks
that receive and sum them. Use channels for communication and join all
tasks. Print the total sum from all consumers.
```

### L5: Translation

Tests the LLM's ability to convert idiomatic code from other languages into idiomatic Hemlock, preserving semantics while using Hemlock-specific features.

**Source languages:** Python, JavaScript, C, Rust, Go

**What makes this hard:**
- Hemlock has no GC (unlike Python/JS/Go) - must add `free()`
- Hemlock's `/` always returns float (unlike C) - must use `divi()`
- Hemlock has `defer` (like Go) but not RAII (like Rust)
- Hemlock strings are mutable (unlike most languages)
- Hemlock has C-style `switch` with fall-through
- Type annotations are optional but checked at runtime

**Example task (L5-M-01):**
```
Prompt: Translate this Python code to idiomatic Hemlock:

def word_frequency(text):
    words = text.lower().split()
    freq = {}
    for word in words:
        if word in freq:
            freq[word] += 1
        else:
            freq[word] = 1
    return sorted(freq.items(), key=lambda x: -x[1])

for word, count in word_frequency("the cat sat on the mat the cat"):
    print(f"{word}: {count}")
```

### L6: Debugging

Tests the LLM's ability to read Hemlock code, identify bugs, and produce corrected versions.

**Bug categories:**
- Off-by-one errors in loops
- Missing `free()` (memory leaks)
- Wrong operator (`/` instead of `divi()` for integer division)
- Missing semicolons
- Type mismatch (passing string where i32 expected)
- Incorrect channel usage (send after close)
- Closure capture bugs
- Pattern matching fallthrough/missing cases
- Incorrect `defer` ordering

**Example task (L6-M-01):**
```
Prompt: This Hemlock program should compute factorial but produces wrong
results. Find and fix all bugs:

fn factorial(n) {
    if (n <= 1) { return 1 }
    return n * factorial(n - 1);
}

let result = factorial(5);
print("5! = " + result)
```
(Bugs: missing semicolons after return 1 and after the print statement)

---

## Evaluation Protocol

### Input Format

Each task provides the LLM with:

1. **System context**: The Hemlock CLAUDE.md documentation (language spec)
2. **Task prompt**: Natural language description of what to write
3. **Expected output**: The exact stdout the program should produce (when deterministic)
4. **Scaffold** (optional): Partial code the LLM must complete

### Scoring

#### Primary Score: Functional Correctness (0 or 1)

```
Run: ./hemlock <generated_file>.hml
Compare: stdout against expected output (exact match, trimmed)
Score: 1 if match, 0 if not
```

For non-deterministic tasks (concurrency), a validator function checks semantic properties instead of exact output.

#### Secondary Scores (0.0 - 1.0 each)

| Metric | How Measured |
|--------|-------------|
| **Syntax validity** | Does the program parse without errors? (0/1) |
| **Runs without crash** | Does it execute without segfault/panic? (0/1) |
| **Output correctness** | Exact match or semantic validator (0/1) |
| **Memory correctness** | No leaks when run under leak checker (0/1, L4 only) |
| **Idiom score** | Does it use Hemlock features appropriately? (manual review, 0-1) |
| **Conciseness** | Lines of code relative to reference solution (ratio, capped at 1.0) |

#### Aggregate Scoring

```
Task score = correctness * 1.0
Level score = mean(task scores within level)
Overall score = weighted mean of level scores

Weights:
  L1 (Syntax):       0.10
  L2 (Stdlib):       0.15
  L3 (Algorithms):   0.20
  L4 (Systems):      0.25
  L5 (Translation):  0.15
  L6 (Debugging):    0.15
```

The weighting emphasizes L4 (systems programming) since that's where Hemlock differentiates from mainstream languages and where memorization helps least.

### Prompt Variants

Each task can be administered in multiple prompt configurations to measure documentation-dependence:

| Variant | Context Provided |
|---------|-----------------|
| **Zero-shot** | Task prompt only (no Hemlock docs) |
| **Doc-guided** | Task prompt + CLAUDE.md (full language spec) |
| **Few-shot** | Task prompt + 2-3 solved examples of similar tasks |
| **Scaffold** | Task prompt + partial code to complete |

Comparing zero-shot vs. doc-guided reveals how well the model can learn from documentation. A large gap suggests the model has little prior Hemlock knowledge (expected) and can follow specs (desirable).

---

## Running the Benchmark

```bash
# Run all tasks
./benchmark/run_benchmark.sh --model <model_id> --variant doc-guided

# Run specific level
./benchmark/run_benchmark.sh --level L3 --model <model_id>

# Run specific task
./benchmark/run_benchmark.sh --task L1-E-01 --model <model_id>

# Generate report
./benchmark/run_benchmark.sh --report results/

# Compare models
./benchmark/run_benchmark.sh --compare results/model_a/ results/model_b/
```

### Output Format

```json
{
  "model": "model-name",
  "variant": "doc-guided",
  "timestamp": "2025-01-15T10:30:00Z",
  "results": {
    "L1-E-01": {
      "task": "fizzbuzz",
      "parses": true,
      "runs": true,
      "correct": true,
      "time_ms": 45,
      "lines": 12
    }
  },
  "summary": {
    "L1": { "total": 10, "correct": 8, "score": 0.80 },
    "overall": 0.72
  }
}
```

---

## Task Index

### L1: Syntax & Basics (10 tasks)
| ID | Difficulty | Topic | Description |
|----|-----------|-------|-------------|
| L1-E-01 | Easy | Control flow | FizzBuzz |
| L1-E-02 | Easy | Functions | Recursive Fibonacci |
| L1-E-03 | Easy | Arrays | Array sum and mean |
| L1-M-01 | Medium | Closures | Counter factory with increment/decrement/reset |
| L1-M-02 | Medium | Objects | Define a Shape type with area methods |
| L1-M-03 | Medium | Pattern matching | Evaluate token types |
| L1-H-01 | Hard | Composition | Expression tree evaluator |
| L1-H-02 | Hard | Null coalescing | Config merger with defaults |
| L1-H-03 | Hard | Iterator pattern | Lazy range with map/filter using closures |
| L1-H-04 | Hard | Named args + types | Type-checked builder pattern |

### L2: Standard Library (8 tasks)
| ID | Difficulty | Topic | Description |
|----|-----------|-------|-------------|
| L2-E-01 | Easy | math | Statistical calculations |
| L2-E-02 | Easy | collections | Phone book with HashMap |
| L2-M-01 | Medium | json + fs | JSON config file processor |
| L2-M-02 | Medium | datetime + fmt | Date range formatter |
| L2-M-03 | Medium | encoding + hash | HMAC-like message signing |
| L2-H-01 | Hard | Multiple modules | CSV-to-JSON pipeline with validation |
| L2-H-02 | Hard | regex + strings | Log file parser with pattern extraction |
| L2-H-03 | Hard | sqlite | In-memory database CRUD operations |

### L3: Algorithms (8 tasks)
| ID | Difficulty | Topic | Description |
|----|-----------|-------|-------------|
| L3-E-01 | Easy | Sorting | Insertion sort implementation |
| L3-E-02 | Easy | Searching | Binary search |
| L3-M-01 | Medium | Trees | Binary search tree (insert, search, traverse) |
| L3-M-02 | Medium | Graphs | BFS shortest path in unweighted graph |
| L3-M-03 | Medium | DP | Longest common subsequence |
| L3-H-01 | Hard | Graphs | Dijkstra's algorithm |
| L3-H-02 | Hard | Strings | Trie with insert, search, prefix-search |
| L3-H-03 | Hard | Composition | Priority queue + task scheduler |

### L4: Systems Programming (8 tasks)
| ID | Difficulty | Topic | Description |
|----|-----------|-------|-------------|
| L4-E-01 | Easy | Memory | Allocate, write, read back, free |
| L4-E-02 | Easy | Defer | Resource cleanup with defer |
| L4-M-01 | Medium | Buffer | Ring buffer implementation |
| L4-M-02 | Medium | Async | Parallel map over array |
| L4-M-03 | Medium | Channels | Pipeline (producer -> transformer -> consumer) |
| L4-H-01 | Hard | Concurrency | Producer-consumer with multiple workers |
| L4-H-02 | Hard | Atomics | Lock-free counter with CAS retry loop |
| L4-H-03 | Hard | FFI + memory | Call C's qsort via FFI on a heap-allocated array |

### L5: Translation (6 tasks)
| ID | Difficulty | Topic | Description |
|----|-----------|-------|-------------|
| L5-E-01 | Easy | Python -> Hemlock | Word frequency counter |
| L5-E-02 | Easy | JS -> Hemlock | Promise chain to async/await |
| L5-M-01 | Medium | C -> Hemlock | Linked list with manual memory |
| L5-M-02 | Medium | Go -> Hemlock | Goroutine fan-out to spawn/channel |
| L5-H-01 | Hard | Rust -> Hemlock | Ownership patterns to manual alloc/free |
| L5-H-02 | Hard | Python -> Hemlock | Class hierarchy to define + duck typing |

### L6: Debugging (6 tasks)
| ID | Difficulty | Topic | Description |
|----|-----------|-------|-------------|
| L6-E-01 | Easy | Syntax | Fix missing semicolons and braces |
| L6-E-02 | Easy | Logic | Fix off-by-one in loop |
| L6-M-01 | Medium | Types | Fix integer division (/ vs divi) |
| L6-M-02 | Medium | Memory | Fix use-after-free and add defer |
| L6-H-01 | Hard | Concurrency | Fix deadlock in channel communication |
| L6-H-02 | Hard | Composition | Fix multiple interacting bugs in a 50-line program |

**Total: 46 tasks**

---

## Design Rationale

### Why Hemlock is a good LLM benchmark target

1. **Novelty**: Hemlock has minimal presence in training data. Models must generalize from documentation rather than recall memorized patterns.

2. **Familiar-but-different**: C-like syntax lures models into false confidence. Subtle differences (`/` returns float, mutable strings, no GC) expose shallow understanding.

3. **Multi-paradigm stress test**: Hemlock combines manual memory (C), closures (JS), pattern matching (Rust/ML), async/channels (Go), and duck typing (Python). Models must compose across paradigms.

4. **Verifiable output**: The existing parity test infrastructure provides exact-match validation. No fuzzy evaluation needed for most tasks.

5. **Difficulty spectrum**: From "print hello world" to "implement a lock-free data structure with FFI" - the benchmark can measure capability at many points.

6. **Documentation quality**: Hemlock's CLAUDE.md is a dense, complete language spec. The benchmark inherently measures a model's ability to follow technical documentation.

### What this benchmark does NOT measure

- Speed of generation (wall-clock time varies by API)
- Cost efficiency (token usage is model-dependent)
- Interactive debugging (this is single-shot generation)
- IDE integration or LSP usage
- Large codebase navigation
