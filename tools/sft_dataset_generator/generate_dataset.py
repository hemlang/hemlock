#!/usr/bin/env python3
"""
Hemlock SFT Dataset Generator

Generates a Supervised Fine-Tuning dataset from Hemlock repository content:
- Tests (parity tests with expected output)
- Examples (example programs)
- Documentation (markdown files with code blocks)

Output: HuggingFace-compatible JSONL format
"""

import json
import os
import re
import random
from pathlib import Path
from typing import Generator

# Repository root (relative to this script)
REPO_ROOT = Path(__file__).parent.parent.parent

# Output directory
OUTPUT_DIR = Path(__file__).parent / "output"

# Instruction templates for variety
OUTPUT_PREDICTION_TEMPLATES = [
    "What does this Hemlock program output?\n\n```hemlock\n{code}\n```",
    "What does the following Hemlock code print?\n\n```hemlock\n{code}\n```",
    "What is the result of running this Hemlock code?\n\n```hemlock\n{code}\n```",
    "Predict the output of this Hemlock program:\n\n```hemlock\n{code}\n```",
    "Run this Hemlock code and show the output:\n\n```hemlock\n{code}\n```",
]

CODE_EXPLANATION_TEMPLATES = [
    "Explain what this Hemlock code does:\n\n```hemlock\n{code}\n```",
    "What is the purpose of this Hemlock code?\n\n```hemlock\n{code}\n```",
    "Describe the behavior of this Hemlock program:\n\n```hemlock\n{code}\n```",
]

CONCEPT_QUESTION_TEMPLATES = [
    "How do you {task} in Hemlock?",
    "Show how to {task} in Hemlock.",
    "What is the Hemlock syntax for {task}?",
    "Provide a Hemlock example that demonstrates {task}.",
]


def random_template(templates: list[str]) -> str:
    """Select a random template from the list."""
    return random.choice(templates)


def create_entry(instruction: str, output: str, category: str, source: str) -> dict:
    """Create a dataset entry."""
    return {
        "instruction": instruction,
        "output": output,
        "category": category,
        "source": source,
    }


# =============================================================================
# PARITY TEST EXTRACTOR
# =============================================================================

def extract_parity_tests() -> Generator[dict, None, None]:
    """Extract instruction/output pairs from parity tests."""
    parity_dir = REPO_ROOT / "tests" / "parity"

    if not parity_dir.exists():
        print(f"Warning: Parity test directory not found: {parity_dir}")
        return

    # Find all .hml files with matching .expected files
    for hml_file in parity_dir.rglob("*.hml"):
        expected_file = hml_file.with_suffix(".expected")

        if not expected_file.exists():
            continue

        try:
            code = hml_file.read_text(encoding="utf-8").strip()
            expected = expected_file.read_text(encoding="utf-8").strip()

            # Skip empty files
            if not code or not expected:
                continue

            # Get category from subdirectory
            rel_path = hml_file.relative_to(parity_dir)
            category = rel_path.parts[0] if len(rel_path.parts) > 1 else "general"

            # Create instruction with random template
            template = random_template(OUTPUT_PREDICTION_TEMPLATES)
            instruction = template.format(code=code)

            yield create_entry(
                instruction=instruction,
                output=expected,
                category=f"parity/{category}",
                source=str(hml_file.relative_to(REPO_ROOT)),
            )

        except Exception as e:
            print(f"Error processing {hml_file}: {e}")


# =============================================================================
# REGULAR TEST EXTRACTOR
# =============================================================================

def extract_regular_tests() -> Generator[dict, None, None]:
    """Extract instruction/output pairs from regular tests with .expected files."""
    tests_dir = REPO_ROOT / "tests"
    parity_dir = tests_dir / "parity"

    if not tests_dir.exists():
        print(f"Warning: Tests directory not found: {tests_dir}")
        return

    # Find all .hml files with matching .expected files (excluding parity)
    for hml_file in tests_dir.rglob("*.hml"):
        # Skip parity tests (already handled)
        if parity_dir in hml_file.parents:
            continue

        expected_file = hml_file.with_suffix(".expected")

        if not expected_file.exists():
            continue

        try:
            code = hml_file.read_text(encoding="utf-8").strip()
            expected = expected_file.read_text(encoding="utf-8").strip()

            # Skip empty files
            if not code or not expected:
                continue

            # Get category from subdirectory
            rel_path = hml_file.relative_to(tests_dir)
            category = rel_path.parts[0] if len(rel_path.parts) > 1 else "general"

            # Create instruction with random template
            template = random_template(OUTPUT_PREDICTION_TEMPLATES)
            instruction = template.format(code=code)

            yield create_entry(
                instruction=instruction,
                output=expected,
                category=f"tests/{category}",
                source=str(hml_file.relative_to(REPO_ROOT)),
            )

        except Exception as e:
            print(f"Error processing {hml_file}: {e}")


# =============================================================================
# DOCUMENTATION EXTRACTOR
# =============================================================================

def extract_code_blocks(markdown_text: str) -> list[tuple[str, str]]:
    """Extract hemlock code blocks from markdown with surrounding context."""
    # Pattern for fenced code blocks with optional language
    pattern = r"```(?:hemlock)?\n(.*?)```"

    blocks = []
    for match in re.finditer(pattern, markdown_text, re.DOTALL):
        code = match.group(1).strip()
        if code:
            # Get context: text before the code block (up to 500 chars)
            start = max(0, match.start() - 500)
            context = markdown_text[start:match.start()].strip()
            # Clean up context - get last paragraph or heading
            context_lines = context.split("\n")
            context_clean = ""
            for line in reversed(context_lines):
                line = line.strip()
                if line.startswith("#"):
                    context_clean = line.lstrip("#").strip()
                    break
                elif line and not line.startswith("```"):
                    context_clean = line
                    break
            blocks.append((code, context_clean))

    return blocks


def extract_section_qa(markdown_text: str, source_file: str) -> Generator[dict, None, None]:
    """Extract Q&A pairs from documentation sections."""
    # Split by headers
    sections = re.split(r"^(#{1,3}\s+.+)$", markdown_text, flags=re.MULTILINE)

    current_header = ""
    for i, section in enumerate(sections):
        if section.startswith("#"):
            current_header = section.lstrip("#").strip()
        elif current_header and section.strip():
            # Extract code blocks from this section
            code_blocks = extract_code_blocks(section)

            for code, context in code_blocks:
                # Skip very short code snippets
                if len(code) < 20:
                    continue

                # Generate instruction based on header/context
                topic = current_header.lower()

                # Create a question about this topic
                if "example" in topic.lower():
                    instruction = f"Show an example of {topic} in Hemlock."
                elif any(word in topic.lower() for word in ["how", "usage", "using"]):
                    instruction = f"How do you use {topic} in Hemlock?"
                else:
                    instruction = f"How does {topic} work in Hemlock?"

                # Output includes code with explanation if context available
                if context and len(context) > 20:
                    output = f"{context}\n\n```hemlock\n{code}\n```"
                else:
                    output = f"```hemlock\n{code}\n```"

                yield create_entry(
                    instruction=instruction,
                    output=output,
                    category="docs",
                    source=source_file,
                )


def extract_documentation() -> Generator[dict, None, None]:
    """Extract instruction/output pairs from documentation."""
    docs_dirs = [
        REPO_ROOT / "docs",
        REPO_ROOT / "stdlib" / "docs",
    ]

    for docs_dir in docs_dirs:
        if not docs_dir.exists():
            print(f"Warning: Docs directory not found: {docs_dir}")
            continue

        for md_file in docs_dir.rglob("*.md"):
            try:
                content = md_file.read_text(encoding="utf-8")
                source = str(md_file.relative_to(REPO_ROOT))

                # Extract Q&A pairs from sections
                yield from extract_section_qa(content, source)

            except Exception as e:
                print(f"Error processing {md_file}: {e}")


# =============================================================================
# EXAMPLE EXTRACTOR
# =============================================================================

def infer_task_from_filename(filename: str) -> str:
    """Infer a task description from the example filename."""
    name = Path(filename).stem

    # Map common filenames to descriptions
    task_map = {
        "42": "print a number",
        "bools": "work with boolean values",
        "countdown": "create a countdown loop",
        "fibonacci": "calculate Fibonacci numbers",
        "alltypes": "demonstrate all Hemlock types",
        "conversions": "convert between types",
        "mixed_math": "perform arithmetic operations",
        "strings": "work with strings",
        "string_manip": "manipulate strings",
        "memory_demo": "manage memory with alloc and free",
        "cli_args": "handle command-line arguments",
        "io_demo": "perform file I/O operations",
        "http_client": "make HTTP requests",
        "http_example": "create an HTTP client",
        "websocket_client": "create a WebSocket client",
        "websocket_server": "create a WebSocket server",
        "functions_demo": "define and use functions",
        "range_check": "validate ranges",
        "types_test": "test type operations",
    }

    # Try to find a match
    for key, desc in task_map.items():
        if key in name.lower():
            return desc

    # Default: convert filename to description
    return name.replace("_", " ").replace("-", " ")


def extract_examples() -> Generator[dict, None, None]:
    """Extract instruction/output pairs from example programs."""
    examples_dir = REPO_ROOT / "examples"

    if not examples_dir.exists():
        print(f"Warning: Examples directory not found: {examples_dir}")
        return

    for hml_file in examples_dir.glob("*.hml"):
        try:
            code = hml_file.read_text(encoding="utf-8").strip()

            # Skip empty files
            if not code:
                continue

            # Skip very large files (> 200 lines)
            if code.count("\n") > 200:
                continue

            # Infer task from filename
            task = infer_task_from_filename(hml_file.name)

            # Create instruction
            template = random_template(CONCEPT_QUESTION_TEMPLATES)
            instruction = template.format(task=task)

            # Output is the code
            output = f"```hemlock\n{code}\n```"

            yield create_entry(
                instruction=instruction,
                output=output,
                category="examples",
                source=str(hml_file.relative_to(REPO_ROOT)),
            )

            # Also create an explanation entry
            template = random_template(CODE_EXPLANATION_TEMPLATES)
            instruction_explain = template.format(code=code)

            # Extract first comment block as explanation
            lines = code.split("\n")
            explanation_lines = []
            for line in lines:
                if line.strip().startswith("//"):
                    explanation_lines.append(line.strip().lstrip("//").strip())
                elif explanation_lines:
                    break

            if explanation_lines:
                explanation = " ".join(explanation_lines)
                yield create_entry(
                    instruction=instruction_explain,
                    output=f"This program {task}. {explanation}",
                    category="examples/explanation",
                    source=str(hml_file.relative_to(REPO_ROOT)),
                )

        except Exception as e:
            print(f"Error processing {hml_file}: {e}")


# =============================================================================
# CONCEPT Q&A EXTRACTOR (from CLAUDE.md)
# =============================================================================

def extract_concepts() -> Generator[dict, None, None]:
    """Extract concept Q&A pairs from CLAUDE.md quick reference."""
    claude_md = REPO_ROOT / "CLAUDE.md"

    if not claude_md.exists():
        print(f"Warning: CLAUDE.md not found: {claude_md}")
        return

    # Predefined concept Q&A pairs based on CLAUDE.md content
    concepts = [
        {
            "instruction": "What integer types are available in Hemlock?",
            "output": "Hemlock provides signed and unsigned integers:\n\n**Signed:** `i8`, `i16`, `i32`, `i64`\n**Unsigned:** `u8`, `u16`, `u32`, `u64`\n\nType aliases: `integer` (i32), `byte` (u8)\n\nLiterals auto-detect size: `42` → i32, `5000000000` → i64 (exceeds i32 max)",
            "category": "concepts/types",
        },
        {
            "instruction": "What floating-point types does Hemlock support?",
            "output": "Hemlock supports two floating-point types:\n\n- `f32` - 32-bit single precision float\n- `f64` - 64-bit double precision float\n\nType alias: `number` (f64)\n\nLiterals default to f64: `3.14` → f64",
            "category": "concepts/types",
        },
        {
            "instruction": "How does type promotion work in Hemlock?",
            "output": "Hemlock promotes types automatically in mixed operations:\n\n`i8 → i16 → i32 → i64 → f32 → f64`\n\nFloats always win in mixed integer/float operations:\n\n```hemlock\nlet i: i32 = 10;\nlet f: f64 = 3.5;\nprint(i + f);  // 13.5 (f64)\n```",
            "category": "concepts/types",
        },
        {
            "instruction": "How do you allocate memory in Hemlock?",
            "output": "Hemlock provides manual memory management:\n\n```hemlock\n// Raw pointer allocation\nlet p = alloc(64);       // allocate 64 bytes\nmemset(p, 0, 64);        // initialize to zero\nfree(p);                 // must free manually\n\n// Safe buffer (bounds checked)\nlet b = buffer(64);\nb[0] = 42;               // bounds checked access\nfree(b);\n\n// Typed allocation\nlet nums = talloc(i32, 10);  // 10 i32 values\nfree(nums);\n```",
            "category": "concepts/memory",
        },
        {
            "instruction": "What is the difference between `ptr` and `buffer` in Hemlock?",
            "output": "`ptr` is a raw pointer with no safety checks - you're responsible for bounds.\n\n`buffer` is a safe wrapper that provides bounds checking:\n\n```hemlock\n// Raw pointer - no bounds checking\nlet p = alloc(10);\np[100] = 1;  // undefined behavior, no error\n\n// Buffer - bounds checked\nlet b = buffer(10);\nb[100] = 1;  // runtime error: index out of bounds\n```\n\nUse `ptr` for performance-critical code, `buffer` for safety.",
            "category": "concepts/memory",
        },
        {
            "instruction": "How do you define a function in Hemlock?",
            "output": "Functions use the `fn` keyword:\n\n```hemlock\n// Basic function\nfn add(a: i32, b: i32): i32 {\n    return a + b;\n}\n\n// Optional parameters with defaults\nfn greet(name: string, msg?: \"Hello\") {\n    print(msg + \" \" + name);\n}\n\n// Anonymous function / closure\nlet double = fn(x) { return x * 2; };\n\n// Usage\nprint(add(2, 3));     // 5\ngreet(\"Alice\");       // Hello Alice\nprint(double(5));     // 10\n```",
            "category": "concepts/functions",
        },
        {
            "instruction": "How does async/await work in Hemlock?",
            "output": "Hemlock has built-in structured concurrency with pthread-based parallelism:\n\n```hemlock\n// Define async function\nasync fn compute(n: i32): i32 {\n    return n * n;\n}\n\n// Spawn a task\nlet task = spawn(compute, 42);\n\n// Wait for result\nlet result = await task;  // or join(task)\nprint(result);  // 1764\n\n// Fire and forget\ndetach(spawn(background_work));\n\n// Channels for communication\nlet ch = channel(10);\nch.send(value);\nlet val = ch.recv();\nch.close();\n```",
            "category": "concepts/async",
        },
        {
            "instruction": "How do you handle errors in Hemlock?",
            "output": "Hemlock uses try/catch/finally for error handling:\n\n```hemlock\ntry {\n    throw \"something went wrong\";\n} catch (e) {\n    print(\"Error: \" + e);\n} finally {\n    cleanup();\n}\n\n// Unrecoverable errors\npanic(\"fatal error\");  // exits immediately, not catchable\n```\n\nUse `throw` for recoverable errors, `panic` for unrecoverable ones.",
            "category": "concepts/errors",
        },
        {
            "instruction": "How do you define an enum in Hemlock?",
            "output": "Enums define named constants:\n\n```hemlock\n// Auto-numbered (0, 1, 2...)\nenum Color {\n    RED,\n    GREEN,\n    BLUE\n}\n\n// Custom values\nenum Status {\n    OK = 0,\n    ERROR = 1,\n    PENDING = 100\n}\n\n// Usage\nlet c = Color.RED;    // 0\nlet s = Status.ERROR; // 1\nprint(c);  // 0\n```",
            "category": "concepts/enums",
        },
        {
            "instruction": "How do you use defer in Hemlock?",
            "output": "`defer` schedules code to run when the function returns:\n\n```hemlock\nfn process_file(path: string) {\n    let f = open(path, \"r\");\n    defer f.close();  // runs when function returns\n    \n    // ... work with file ...\n    \n    if (error) {\n        return;  // f.close() still runs\n    }\n    \n    // f.close() runs here too\n}\n```\n\nDeferred statements run in reverse order (LIFO).",
            "category": "concepts/defer",
        },
        {
            "instruction": "What string methods are available in Hemlock?",
            "output": "Hemlock strings have 19 methods:\n\n**Slicing:** `substr(start, len)`, `slice(start, end)`\n**Search:** `find(needle)`, `contains(str)`\n**Transform:** `split(delim)`, `trim()`, `to_upper()`, `to_lower()`\n**Check:** `starts_with(prefix)`, `ends_with(suffix)`\n**Replace:** `replace(old, new)`, `replace_all(old, new)`\n**Other:** `repeat(n)`, `char_at(i)`, `byte_at(i)`, `chars()`, `bytes()`, `to_bytes()`, `deserialize()`\n\n```hemlock\nlet s = \"hello world\";\nprint(s.length);           // 11\nprint(s.substr(0, 5));     // \"hello\"\nprint(s.split(\" \")[0]);   // \"hello\"\nprint(s.to_upper());       // \"HELLO WORLD\"\n```",
            "category": "concepts/strings",
        },
        {
            "instruction": "What array methods are available in Hemlock?",
            "output": "Hemlock arrays have 18 methods:\n\n**Mutating:** `push(val)`, `pop()`, `shift()`, `unshift(val)`, `insert(i, val)`, `remove(i)`, `clear()`, `reverse()`\n**Access:** `first()`, `last()`, `slice(start, end)`\n**Search:** `find(val)`, `contains(val)`\n**Transform:** `join(sep)`, `concat(arr)`, `map(fn)`, `filter(fn)`, `reduce(fn, init)`\n\n```hemlock\nlet arr = [1, 2, 3];\narr.push(4);              // [1, 2, 3, 4]\nlet doubled = arr.map(fn(x) { return x * 2; });  // [2, 4, 6, 8]\nlet sum = arr.reduce(fn(a, b) { return a + b; }, 0);  // 10\n```",
            "category": "concepts/arrays",
        },
        {
            "instruction": "How do you import modules in Hemlock?",
            "output": "Use `import` with the `@stdlib/` prefix for standard library:\n\n```hemlock\n// Import specific items\nimport { sin, cos, PI } from \"@stdlib/math\";\n\n// Import all as namespace\nimport * as math from \"@stdlib/math\";\nlet result = math.sqrt(16);\n\n// Import from local files\nimport { helper } from \"./utils.hml\";\n```\n\nStandard library modules: math, collections, fs, net, json, http, crypto, regex, time, datetime, encoding, hash, logging, terminal, testing, and more.",
            "category": "concepts/modules",
        },
        {
            "instruction": "How do you read and write files in Hemlock?",
            "output": "Use the `open()` function with mode strings:\n\n```hemlock\n// Read file\nlet f = open(\"data.txt\", \"r\");\nlet content = f.read();\nf.close();\n\n// Write file\nlet f = open(\"output.txt\", \"w\");\nf.write(\"Hello, World!\");\nf.close();\n\n// Append to file\nlet f = open(\"log.txt\", \"a\");\nf.write(\"New entry\\n\");\nf.close();\n\n// Modes: r, w, a, r+, w+, a+\n```\n\nOr use the fs stdlib module:\n\n```hemlock\nimport { read_file, write_file } from \"@stdlib/fs\";\nlet content = read_file(\"data.txt\");\nwrite_file(\"output.txt\", content);\n```",
            "category": "concepts/io",
        },
        {
            "instruction": "How do you use FFI to call C functions in Hemlock?",
            "output": "Use `ffi_open`, `ffi_bind`, and `ffi_close`:\n\n```hemlock\n// Open shared library\nlet lib = ffi_open(\"libc.so.6\");\n\n// Bind function: name, arg types, return type\nlet puts = ffi_bind(lib, \"puts\", [FFI_POINTER], FFI_INT);\n\n// Call the function\nputs(\"Hello from C!\");\n\n// Clean up\nffi_close(lib);\n```\n\nFFI types: `FFI_INT`, `FFI_DOUBLE`, `FFI_POINTER`, `FFI_STRING`, `FFI_VOID`, etc.",
            "category": "concepts/ffi",
        },
        {
            "instruction": "What is the difference between `/` and `div()` in Hemlock?",
            "output": "The `/` operator always returns a float, while `div()` (or `divi()`) performs floor division:\n\n```hemlock\nprint(7 / 2);      // 3.5 (float division)\nprint(div(7, 2));  // 3 (floor division)\nprint(7 % 2);      // 1 (remainder)\n```\n\nThis differs from C where `/` on integers gives integer division.",
            "category": "concepts/operators",
        },
        {
            "instruction": "How do you define objects in Hemlock?",
            "output": "Hemlock supports object literals and defined types:\n\n```hemlock\n// Anonymous object\nlet point = { x: 10, y: 20 };\nprint(point.x);  // 10\n\n// Defined type with defaults\ndefine Person {\n    name: string,\n    age: i32,\n    active?: true  // optional with default\n}\n\nlet p: Person = { name: \"Alice\", age: 30 };\nprint(p.active);  // true (default)\n\n// Serialization\nlet json = p.serialize();\nlet restored = json.deserialize();\n```",
            "category": "concepts/objects",
        },
        {
            "instruction": "What control flow statements does Hemlock have?",
            "output": "Hemlock has standard control flow with mandatory braces:\n\n```hemlock\n// Conditionals\nif (x > 0) {\n    print(\"positive\");\n} else if (x < 0) {\n    print(\"negative\");\n} else {\n    print(\"zero\");\n}\n\n// While loop\nwhile (condition) {\n    break;     // exit loop\n    continue;  // next iteration\n}\n\n// For loop\nfor (let i = 0; i < 10; i = i + 1) {\n    print(i);\n}\n\n// For-in loop\nfor (item in array) {\n    print(item);\n}\n\n// Switch\nswitch (x) {\n    case 1: print(\"one\"); break;\n    case 2: print(\"two\"); break;\n    default: print(\"other\"); break;\n}\n```",
            "category": "concepts/control-flow",
        },
    ]

    for concept in concepts:
        yield create_entry(
            instruction=concept["instruction"],
            output=concept["output"],
            category=concept["category"],
            source="CLAUDE.md",
        )


# =============================================================================
# MAIN GENERATOR
# =============================================================================

def generate_dataset(seed: int = 42) -> list[dict]:
    """Generate the complete dataset."""
    random.seed(seed)

    dataset = []

    print("Extracting parity tests...")
    parity_entries = list(extract_parity_tests())
    print(f"  Found {len(parity_entries)} parity test entries")
    dataset.extend(parity_entries)

    print("Extracting regular tests...")
    test_entries = list(extract_regular_tests())
    print(f"  Found {len(test_entries)} regular test entries")
    dataset.extend(test_entries)

    print("Extracting documentation...")
    doc_entries = list(extract_documentation())
    print(f"  Found {len(doc_entries)} documentation entries")
    dataset.extend(doc_entries)

    print("Extracting examples...")
    example_entries = list(extract_examples())
    print(f"  Found {len(example_entries)} example entries")
    dataset.extend(example_entries)

    print("Extracting concept Q&A...")
    concept_entries = list(extract_concepts())
    print(f"  Found {len(concept_entries)} concept entries")
    dataset.extend(concept_entries)

    print(f"\nTotal entries: {len(dataset)}")

    return dataset


def write_jsonl(dataset: list[dict], output_path: Path) -> None:
    """Write dataset to JSONL file."""
    with open(output_path, "w", encoding="utf-8") as f:
        for entry in dataset:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")


def write_readme(dataset: list[dict], output_path: Path) -> None:
    """Write dataset README/card for HuggingFace."""
    # Count by category
    categories = {}
    for entry in dataset:
        cat = entry.get("category", "unknown")
        categories[cat] = categories.get(cat, 0) + 1

    readme = f"""---
license: mit
task_categories:
  - text-generation
language:
  - en
tags:
  - hemlock
  - programming-language
  - code
  - sft
pretty_name: Hemlock SFT Dataset
size_categories:
  - 1K<n<10K
---

# Hemlock SFT Dataset

A Supervised Fine-Tuning dataset for the Hemlock programming language.

## Dataset Description

This dataset contains instruction/output pairs for training language models on the Hemlock programming language. It covers:

- Language syntax and semantics
- Standard library usage
- Code examples and explanations
- Concept Q&A

## Dataset Statistics

- **Total entries:** {len(dataset)}
- **Format:** JSONL

### Entries by Category

| Category | Count |
|----------|-------|
"""

    for cat, count in sorted(categories.items(), key=lambda x: -x[1]):
        readme += f"| {cat} | {count} |\n"

    readme += """
## Dataset Format

Each entry contains:

```json
{
  "instruction": "The prompt or question",
  "output": "The expected response",
  "category": "Category for filtering",
  "source": "Original source file"
}
```

## Usage

```python
from datasets import load_dataset

dataset = load_dataset("json", data_files="hemlock_sft_dataset.jsonl")
```

## License

MIT License - Same as the Hemlock language.

## Source

Generated from the [Hemlock repository](https://github.com/hemlang/hemlock) including:
- Tests (parity tests with expected output)
- Documentation (language guide, stdlib docs)
- Example programs
- CLAUDE.md reference
"""

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(readme)


def main():
    """Main entry point."""
    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Generate dataset
    dataset = generate_dataset()

    # Shuffle dataset
    random.shuffle(dataset)

    # Write outputs
    jsonl_path = OUTPUT_DIR / "hemlock_sft_dataset.jsonl"
    readme_path = OUTPUT_DIR / "README.md"

    print(f"\nWriting {jsonl_path}...")
    write_jsonl(dataset, jsonl_path)

    print(f"Writing {readme_path}...")
    write_readme(dataset, readme_path)

    print("\nDone!")
    print(f"Dataset: {jsonl_path}")
    print(f"README: {readme_path}")

    # Print sample entry
    if dataset:
        print("\nSample entry:")
        print(json.dumps(dataset[0], indent=2, ensure_ascii=False)[:500] + "...")


if __name__ == "__main__":
    main()
