#!/usr/bin/env python3
"""
Hemlock SFT Dataset Generator

Generates a Supervised Fine-Tuning dataset from Hemlock repository content:
- Tests → "Write a Hemlock program that [description]" → code
- Examples → "Write a Hemlock program that [description]" → code
- Documentation → Q&A about Hemlock features → explanations with code

Output: HuggingFace-compatible JSONL format
"""

import json
import re
import random
from pathlib import Path
from typing import Generator

# Repository root (relative to this script)
REPO_ROOT = Path(__file__).parent.parent.parent

# Output directory
OUTPUT_DIR = Path(__file__).parent / "output"

# Instruction templates for code generation
CODE_GEN_TEMPLATES = [
    "Write a Hemlock program that {task}.",
    "Write Hemlock code that {task}.",
    "Create a Hemlock program that {task}.",
    "Implement a Hemlock program that {task}.",
    "Show me Hemlock code that {task}.",
]

EXAMPLE_TEMPLATES = [
    "Write a Hemlock example that demonstrates {task}.",
    "Show me how to {task} in Hemlock.",
    "Provide a Hemlock example of {task}.",
    "Give me Hemlock code that shows {task}.",
]


def random_template(templates: list[str]) -> str:
    """Select a random template from the list."""
    return random.choice(templates)


def create_entry(instruction: str, output: str, category: str, source: str) -> dict:
    """Create a dataset entry."""
    return {
        "instruction": instruction.strip(),
        "output": output.strip(),
        "category": category,
        "source": source,
    }


def extract_description_from_code(code: str) -> str | None:
    """Extract a description from code comments."""
    lines = code.split("\n")
    description_lines = []

    for line in lines:
        line = line.strip()
        if line.startswith("//"):
            comment = line.lstrip("/").strip()
            # Skip empty comments and file markers
            if comment and not comment.startswith("Test:") and not comment.startswith("Category:"):
                description_lines.append(comment)
        elif description_lines:
            # Stop at first non-comment line after we've found comments
            break

    if description_lines:
        desc = " ".join(description_lines)
        # Clean up the description
        desc = desc.strip()
        # Remove trailing punctuation and normalize
        desc = desc.rstrip(".")
        # Make it lowercase for use in templates
        if desc and desc[0].isupper():
            desc = desc[0].lower() + desc[1:]
        return desc

    return None


def extract_description_from_filename(filename: str) -> str:
    """Generate a description from filename."""
    name = Path(filename).stem

    # Map common test names to descriptions
    name_map = {
        "arithmetic": "tests arithmetic operations",
        "strings": "tests string operations",
        "arrays": "tests array operations",
        "objects": "tests object operations",
        "functions": "tests function definitions and calls",
        "closures": "tests closures and captured variables",
        "async": "tests async/await functionality",
        "channels": "tests channel communication",
        "defer": "tests defer statements",
        "switch": "tests switch statements",
        "loops": "tests loop constructs",
        "if_else": "tests conditional statements",
        "try_catch": "tests exception handling",
        "enums": "tests enum definitions",
        "typed_arrays": "tests typed arrays",
        "memory": "tests memory allocation",
        "bitwise": "tests bitwise operations",
        "typeof": "tests the typeof operator",
        "print": "tests the print function",
        "assert": "tests assertions",
        "serialization": "tests JSON serialization",
        "fibonacci": "calculates Fibonacci numbers",
        "factorial": "calculates factorial",
    }

    # Check for direct match
    name_lower = name.lower()
    for key, desc in name_map.items():
        if key in name_lower:
            return desc

    # Convert filename to description
    desc = name.replace("_", " ").replace("-", " ")
    return f"tests {desc}"


def clean_markdown_context(text: str) -> str:
    """Clean markdown artifacts from context text."""
    # Remove list numbering (1., 2., etc.)
    text = re.sub(r"^\d+\.\s*", "", text)
    # Remove bullet points
    text = re.sub(r"^[-*]\s*", "", text)
    # Remove markdown bold/italic
    text = re.sub(r"\*\*([^*]+)\*\*", r"\1", text)
    text = re.sub(r"\*([^*]+)\*", r"\1", text)
    text = re.sub(r"__([^_]+)__", r"\1", text)
    text = re.sub(r"_([^_]+)_", r"\1", text)
    # Remove backticks around inline code
    text = re.sub(r"`([^`]+)`", r"\1", text)
    # Remove markdown links [text](url) -> text
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    # Clean up extra whitespace
    text = " ".join(text.split())
    return text.strip()


# =============================================================================
# TEST EXTRACTOR - "Write code that [description]" → code
# =============================================================================

def extract_tests() -> Generator[dict, None, None]:
    """Extract code generation pairs from all tests."""
    tests_dir = REPO_ROOT / "tests"

    if not tests_dir.exists():
        print(f"Warning: Tests directory not found: {tests_dir}")
        return

    for hml_file in tests_dir.rglob("*.hml"):
        try:
            code = hml_file.read_text(encoding="utf-8").strip()

            # Skip empty or very short files
            if not code or len(code) < 30:
                continue

            # Skip very large files (> 150 lines)
            if code.count("\n") > 150:
                continue

            # Get category from path
            rel_path = hml_file.relative_to(tests_dir)
            if len(rel_path.parts) > 1:
                category = rel_path.parts[0]
            else:
                category = "general"

            # Try to extract description from comments
            desc = extract_description_from_code(code)

            # Fall back to filename-based description
            if not desc:
                desc = extract_description_from_filename(hml_file.name)

            # Create instruction
            template = random_template(CODE_GEN_TEMPLATES)
            instruction = template.format(task=desc)

            # Output is the code wrapped in hemlock fence
            output = f"```hemlock\n{code}\n```"

            yield create_entry(
                instruction=instruction,
                output=output,
                category=f"tests/{category}",
                source=str(hml_file.relative_to(REPO_ROOT)),
            )

        except Exception as e:
            print(f"Error processing {hml_file}: {e}")


# =============================================================================
# EXAMPLE EXTRACTOR
# =============================================================================

def extract_examples() -> Generator[dict, None, None]:
    """Extract code generation pairs from example programs."""
    examples_dir = REPO_ROOT / "examples"

    if not examples_dir.exists():
        print(f"Warning: Examples directory not found: {examples_dir}")
        return

    # Map filenames to better descriptions
    example_descriptions = {
        "42.hml": "prints the number 42",
        "bools.hml": "demonstrates boolean operations",
        "countdown.hml": "implements a countdown loop",
        "fibonacci.hml": "calculates Fibonacci numbers recursively",
        "alltypes.hml": "demonstrates all Hemlock data types",
        "conversions.hml": "converts between different types",
        "mixed_math.hml": "performs mixed arithmetic operations",
        "strings.hml": "demonstrates string operations",
        "string_manip.hml": "manipulates strings",
        "memory_demo.hml": "demonstrates memory management with alloc, buffer, and free",
        "cli_args.hml": "handles command-line arguments",
        "io_demo.hml": "demonstrates file I/O operations",
        "http_client.hml": "makes HTTP requests",
        "http_example.hml": "demonstrates the HTTP client library",
        "websocket_client_lws.hml": "creates a WebSocket client",
        "websocket_echo_client.hml": "creates a WebSocket echo client",
        "websocket_server_lws.hml": "creates a WebSocket server",
        "functions_demo.hml": "demonstrates function definitions and closures",
        "range_check.hml": "validates numeric ranges",
        "types_test.hml": "tests type operations",
    }

    for hml_file in examples_dir.glob("*.hml"):
        try:
            code = hml_file.read_text(encoding="utf-8").strip()

            # Skip empty files
            if not code:
                continue

            # Skip very large files
            if code.count("\n") > 200:
                continue

            # Get description
            desc = example_descriptions.get(hml_file.name)
            if not desc:
                desc = extract_description_from_code(code)
            if not desc:
                desc = extract_description_from_filename(hml_file.name)

            # Create instruction
            template = random_template(EXAMPLE_TEMPLATES)
            instruction = template.format(task=desc)

            # Output is the code
            output = f"```hemlock\n{code}\n```"

            yield create_entry(
                instruction=instruction,
                output=output,
                category="examples",
                source=str(hml_file.relative_to(REPO_ROOT)),
            )

        except Exception as e:
            print(f"Error processing {hml_file}: {e}")


# =============================================================================
# DOCUMENTATION EXTRACTOR
# =============================================================================

def extract_documentation() -> Generator[dict, None, None]:
    """Extract Q&A pairs from documentation."""
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

                yield from extract_doc_sections(content, source)

            except Exception as e:
                print(f"Error processing {md_file}: {e}")


def extract_doc_sections(markdown_text: str, source_file: str) -> Generator[dict, None, None]:
    """Extract Q&A pairs from documentation sections with code blocks."""

    # Pattern for fenced code blocks
    code_pattern = r"```hemlock\n(.*?)```"

    # Split by headers (##, ###)
    sections = re.split(r"^(#{2,3}\s+.+)$", markdown_text, flags=re.MULTILINE)

    current_header = ""
    for section in sections:
        section = section.strip()

        if section.startswith("#"):
            current_header = section.lstrip("#").strip()
            # Clean header
            current_header = clean_markdown_context(current_header)
        elif current_header and section:
            # Find code blocks in this section
            code_matches = list(re.finditer(code_pattern, section, re.DOTALL))

            if not code_matches:
                continue

            for match in code_matches:
                code = match.group(1).strip()

                # Skip very short code snippets
                if len(code) < 15:
                    continue

                # Get context before the code block
                text_before = section[:match.start()].strip()

                # Get the most relevant context line
                context_lines = text_before.split("\n")
                context = ""
                for line in reversed(context_lines):
                    line = line.strip()
                    # Skip empty lines, code fence markers, table rows
                    if not line or line.startswith("```") or line.startswith("|"):
                        continue
                    context = clean_markdown_context(line)
                    break

                # Generate instruction based on header
                header_lower = current_header.lower()

                # Skip generic or navigation headers
                skip_headers = ["see also", "related", "contents", "overview", "table of contents"]
                if any(skip in header_lower for skip in skip_headers):
                    continue

                # Create appropriate instruction
                if "example" in header_lower:
                    instruction = f"Show an example of {current_header.lower()} in Hemlock."
                elif any(word in header_lower for word in ["how to", "usage", "using"]):
                    instruction = f"How do you {current_header.lower()} in Hemlock?"
                else:
                    instruction = f"How does {current_header.lower()} work in Hemlock?"

                # Build output with context if meaningful
                if context and len(context) > 15 and not context.startswith("```"):
                    output = f"{context}\n\n```hemlock\n{code}\n```"
                else:
                    output = f"```hemlock\n{code}\n```"

                yield create_entry(
                    instruction=instruction,
                    output=output,
                    category="docs",
                    source=source_file,
                )


# =============================================================================
# CONCEPT Q&A (Curated high-quality entries)
# =============================================================================

def extract_concepts() -> Generator[dict, None, None]:
    """Curated concept Q&A pairs for core language features."""

    concepts = [
        # Types
        {
            "instruction": "What integer types are available in Hemlock?",
            "output": "Hemlock provides signed and unsigned integers:\n\n**Signed:** i8, i16, i32, i64\n**Unsigned:** u8, u16, u32, u64\n\nType aliases: `integer` (i32), `byte` (u8)\n\nLiterals auto-detect size:\n```hemlock\nlet x = 42;           // i32\nlet big = 5000000000; // i64 (exceeds i32 max)\nlet hex = 0xDEADBEEF; // hex literal\nlet bin = 0b1010;     // binary literal\n```",
            "category": "concepts/types",
        },
        {
            "instruction": "What floating-point types does Hemlock support?",
            "output": "Hemlock supports two floating-point types:\n\n- `f32` - 32-bit single precision\n- `f64` - 64-bit double precision\n\nType alias: `number` (f64)\n\n```hemlock\nlet pi = 3.14;        // f64 by default\nlet f: f32 = 3.14;    // explicit f32\n```",
            "category": "concepts/types",
        },
        {
            "instruction": "How does type promotion work in Hemlock?",
            "output": "Hemlock promotes types automatically in mixed operations:\n\ni8 → i16 → i32 → i64 → f32 → f64\n\nFloats always win in mixed integer/float operations:\n\n```hemlock\nlet i: i32 = 10;\nlet f: f64 = 3.5;\nprint(i + f);  // 13.5 (f64)\n```",
            "category": "concepts/types",
        },
        # Memory
        {
            "instruction": "How do you allocate memory in Hemlock?",
            "output": "Hemlock provides manual memory management:\n\n```hemlock\n// Raw pointer allocation\nlet p = alloc(64);       // allocate 64 bytes\nmemset(p, 0, 64);        // initialize to zero\nfree(p);                 // must free manually\n\n// Safe buffer (bounds checked)\nlet b = buffer(64);\nb[0] = 42;               // bounds checked access\nfree(b);\n\n// Typed allocation\nlet nums = talloc(i32, 10);  // 10 i32 values\nfree(nums);\n```",
            "category": "concepts/memory",
        },
        {
            "instruction": "What is the difference between ptr and buffer in Hemlock?",
            "output": "`ptr` is a raw pointer with no safety checks - you're responsible for bounds.\n\n`buffer` is a safe wrapper that provides bounds checking:\n\n```hemlock\n// Raw pointer - no bounds checking\nlet p = alloc(10);\np[100] = 1;  // undefined behavior, no error\n\n// Buffer - bounds checked\nlet b = buffer(10);\nb[100] = 1;  // runtime error: index out of bounds\n```\n\nUse `ptr` for performance-critical code, `buffer` for safety.",
            "category": "concepts/memory",
        },
        # Functions
        {
            "instruction": "How do you define a function in Hemlock?",
            "output": "Functions use the `fn` keyword:\n\n```hemlock\n// Basic function with types\nfn add(a: i32, b: i32): i32 {\n    return a + b;\n}\n\n// Optional parameters with defaults\nfn greet(name: string, msg?: \"Hello\") {\n    print(msg + \" \" + name);\n}\n\n// Anonymous function / closure\nlet double = fn(x) { return x * 2; };\n\n// Usage\nprint(add(2, 3));     // 5\ngreet(\"Alice\");       // Hello Alice\nprint(double(5));     // 10\n```",
            "category": "concepts/functions",
        },
        # Async
        {
            "instruction": "How does async/await work in Hemlock?",
            "output": "Hemlock has built-in structured concurrency with pthread-based parallelism:\n\n```hemlock\n// Define async function\nasync fn compute(n: i32): i32 {\n    return n * n;\n}\n\n// Spawn a task\nlet task = spawn(compute, 42);\n\n// Wait for result\nlet result = await task;  // or join(task)\nprint(result);  // 1764\n\n// Fire and forget\ndetach(spawn(background_work));\n\n// Channels for communication\nlet ch = channel(10);\nch.send(value);\nlet val = ch.recv();\nch.close();\n```",
            "category": "concepts/async",
        },
        # Error handling
        {
            "instruction": "How do you handle errors in Hemlock?",
            "output": "Hemlock uses try/catch/finally for error handling:\n\n```hemlock\ntry {\n    throw \"something went wrong\";\n} catch (e) {\n    print(\"Error: \" + e);\n} finally {\n    cleanup();\n}\n\n// Unrecoverable errors\npanic(\"fatal error\");  // exits immediately, not catchable\n```\n\nUse `throw` for recoverable errors, `panic` for unrecoverable ones.",
            "category": "concepts/errors",
        },
        # Enums
        {
            "instruction": "How do you define an enum in Hemlock?",
            "output": "Enums define named constants:\n\n```hemlock\n// Auto-numbered (0, 1, 2...)\nenum Color {\n    RED,\n    GREEN,\n    BLUE\n}\n\n// Custom values\nenum Status {\n    OK = 0,\n    ERROR = 1,\n    PENDING = 100\n}\n\n// Usage\nlet c = Color.RED;    // 0\nlet s = Status.ERROR; // 1\nprint(c);  // 0\n```",
            "category": "concepts/enums",
        },
        # Defer
        {
            "instruction": "How do you use defer in Hemlock?",
            "output": "`defer` schedules code to run when the function returns:\n\n```hemlock\nfn process_file(path: string) {\n    let f = open(path, \"r\");\n    defer f.close();  // runs when function returns\n    \n    // ... work with file ...\n    \n    if (error) {\n        return;  // f.close() still runs\n    }\n    \n    // f.close() runs here too\n}\n```\n\nDeferred statements run in reverse order (LIFO).",
            "category": "concepts/defer",
        },
        # Strings
        {
            "instruction": "What string methods are available in Hemlock?",
            "output": "Hemlock strings have 19 methods:\n\n**Slicing:** substr(start, len), slice(start, end)\n**Search:** find(needle), contains(str)\n**Transform:** split(delim), trim(), to_upper(), to_lower()\n**Check:** starts_with(prefix), ends_with(suffix)\n**Replace:** replace(old, new), replace_all(old, new)\n**Other:** repeat(n), char_at(i), byte_at(i), chars(), bytes(), to_bytes(), deserialize()\n\n```hemlock\nlet s = \"hello world\";\nprint(s.length);           // 11\nprint(s.substr(0, 5));     // \"hello\"\nprint(s.split(\" \")[0]);   // \"hello\"\nprint(s.to_upper());       // \"HELLO WORLD\"\n```",
            "category": "concepts/strings",
        },
        # Arrays
        {
            "instruction": "What array methods are available in Hemlock?",
            "output": "Hemlock arrays have 18 methods:\n\n**Mutating:** push(val), pop(), shift(), unshift(val), insert(i, val), remove(i), clear(), reverse()\n**Access:** first(), last(), slice(start, end)\n**Search:** find(val), contains(val)\n**Transform:** join(sep), concat(arr), map(fn), filter(fn), reduce(fn, init)\n\n```hemlock\nlet arr = [1, 2, 3];\narr.push(4);              // [1, 2, 3, 4]\nlet doubled = arr.map(fn(x) { return x * 2; });  // [2, 4, 6, 8]\nlet sum = arr.reduce(fn(a, b) { return a + b; }, 0);  // 10\n```",
            "category": "concepts/arrays",
        },
        # Modules
        {
            "instruction": "How do you import modules in Hemlock?",
            "output": "Use `import` with the `@stdlib/` prefix for standard library:\n\n```hemlock\n// Import specific items\nimport { sin, cos, PI } from \"@stdlib/math\";\n\n// Import all as namespace\nimport * as math from \"@stdlib/math\";\nlet result = math.sqrt(16);\n\n// Import from local files\nimport { helper } from \"./utils.hml\";\n```\n\nStandard library modules: math, collections, fs, net, json, http, crypto, regex, time, datetime, encoding, hash, logging, terminal, testing, and more.",
            "category": "concepts/modules",
        },
        # File I/O
        {
            "instruction": "How do you read and write files in Hemlock?",
            "output": "Use the `open()` function with mode strings:\n\n```hemlock\n// Read file\nlet f = open(\"data.txt\", \"r\");\nlet content = f.read();\nf.close();\n\n// Write file\nlet f = open(\"output.txt\", \"w\");\nf.write(\"Hello, World!\");\nf.close();\n\n// Append to file\nlet f = open(\"log.txt\", \"a\");\nf.write(\"New entry\\n\");\nf.close();\n```\n\nModes: r, w, a, r+, w+, a+\n\nOr use the fs stdlib:\n```hemlock\nimport { read_file, write_file } from \"@stdlib/fs\";\nlet content = read_file(\"data.txt\");\nwrite_file(\"output.txt\", content);\n```",
            "category": "concepts/io",
        },
        # FFI
        {
            "instruction": "How do you call C functions from Hemlock using FFI?",
            "output": "Use ffi_open, ffi_bind, and ffi_close:\n\n```hemlock\n// Open shared library\nlet lib = ffi_open(\"libc.so.6\");\n\n// Bind function: name, arg types, return type\nlet puts = ffi_bind(lib, \"puts\", [FFI_POINTER], FFI_INT);\n\n// Call the function\nputs(\"Hello from C!\");\n\n// Clean up\nffi_close(lib);\n```\n\nFFI types: FFI_INT, FFI_DOUBLE, FFI_POINTER, FFI_STRING, FFI_VOID, etc.",
            "category": "concepts/ffi",
        },
        # Division
        {
            "instruction": "What is the difference between / and div() in Hemlock?",
            "output": "The `/` operator always returns a float, while `div()` performs floor division:\n\n```hemlock\nprint(7 / 2);      // 3.5 (float division)\nprint(div(7, 2));  // 3 (floor division)\nprint(7 % 2);      // 1 (remainder)\n```\n\nThis differs from C where `/` on integers gives integer division.",
            "category": "concepts/operators",
        },
        # Objects
        {
            "instruction": "How do you define objects in Hemlock?",
            "output": "Hemlock supports object literals and defined types:\n\n```hemlock\n// Anonymous object\nlet point = { x: 10, y: 20 };\nprint(point.x);  // 10\n\n// Defined type with defaults\ndefine Person {\n    name: string,\n    age: i32,\n    active?: true  // optional with default\n}\n\nlet p: Person = { name: \"Alice\", age: 30 };\nprint(p.active);  // true (default)\n\n// Serialization\nlet json = p.serialize();\nlet restored = json.deserialize();\n```",
            "category": "concepts/objects",
        },
        # Control flow
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

    print("Extracting tests...")
    test_entries = list(extract_tests())
    print(f"  Found {len(test_entries)} test entries")
    dataset.extend(test_entries)

    print("Extracting examples...")
    example_entries = list(extract_examples())
    print(f"  Found {len(example_entries)} example entries")
    dataset.extend(example_entries)

    print("Extracting documentation...")
    doc_entries = list(extract_documentation())
    print(f"  Found {len(doc_entries)} documentation entries")
    dataset.extend(doc_entries)

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

This dataset contains instruction/output pairs for training language models on the Hemlock programming language:

- **Code generation**: "Write Hemlock code that..." → code
- **Concept Q&A**: Questions about Hemlock features → explanations with code
- **Documentation**: Feature explanations with examples

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

```json
{
  "instruction": "Write a Hemlock program that tests arithmetic operations.",
  "output": "```hemlock\\nlet x = 5 + 3;\\nprint(x);  // 8\\n```",
  "category": "tests/arithmetic",
  "source": "tests/arithmetic/basic.hml"
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

Generated from the [Hemlock repository](https://github.com/hemlang/hemlock).
"""

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(readme)


def main():
    """Main entry point."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    dataset = generate_dataset()

    # Shuffle
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

    # Print sample entries
    if dataset:
        print("\n--- Sample Entries ---")
        for i, entry in enumerate(dataset[:3]):
            print(f"\n[{i+1}] {entry['category']}")
            print(f"Instruction: {entry['instruction'][:80]}...")
            print(f"Output: {entry['output'][:100]}...")


if __name__ == "__main__":
    main()
