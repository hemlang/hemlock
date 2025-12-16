---
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

- **Total entries:** 1959
- **Format:** JSONL

### Entries by Category

| Category | Count |
|----------|-------|
| docs | 1308 |
| tests/parity | 72 |
| tests/compiler | 50 |
| tests/objects | 42 |
| tests/arrays | 38 |
| tests/strings | 30 |
| tests/async | 25 |
| tests/primitives | 23 |
| examples | 23 |
| tests/exceptions | 20 |
| tests/switch | 19 |
| tests/io | 18 |
| tests/functions | 17 |
| tests/manual | 15 |
| tests/control | 15 |
| tests/stdlib_regex | 11 |
| tests/memory | 11 |
| tests/arithmetic | 10 |
| tests/const | 9 |
| tests/defer | 9 |
| tests/stdlib_terminal | 9 |
| tests/loops | 8 |
| tests/async_io | 8 |
| tests/bitwise | 8 |
| tests/signals | 7 |
| tests/stdlib_logging | 7 |
| tests/typed_arrays | 7 |
| tests/enums | 7 |
| tests/stdlib_compression | 6 |
| tests/circular_refs | 6 |
| tests/stdlib_math | 6 |
| tests/buffers | 5 |
| tests/optional_chaining | 5 |
| tests/stdlib_crypto | 5 |
| tests/stdlib_json | 5 |
| tests/stdlib_datetime | 5 |
| tests/comparisons | 5 |
| tests/stdlib_strings | 5 |
| tests/args | 5 |
| tests/ffi | 4 |
| tests/modules | 4 |
| tests/pointers | 4 |
| tests/stdlib_collections | 4 |
| tests/variables | 4 |
| tests/stdlib_net | 3 |
| concepts/types | 3 |
| tests/stdlib_testing | 3 |
| tests/interpolation | 3 |
| tests/networking | 3 |
| tests/stdlib_hash | 3 |
| tests/conversions | 3 |
| tests/stdlib_encoding | 3 |
| tests/stdlib_websocket | 3 |
| tests/stdlib_env | 2 |
| tests/ffi_callbacks | 2 |
| tests/stdlib_time | 2 |
| tests/shebang | 2 |
| tests/bools | 2 |
| concepts/memory | 2 |
| concepts/defer | 1 |
| concepts/errors | 1 |
| concepts/functions | 1 |
| concepts/arrays | 1 |
| concepts/ffi | 1 |
| concepts/control-flow | 1 |
| concepts/objects | 1 |
| concepts/strings | 1 |
| concepts/modules | 1 |
| concepts/async | 1 |
| concepts/io | 1 |
| tests/exec | 1 |
| concepts/operators | 1 |
| tests/stdlib_http | 1 |
| tests/stdlib_process | 1 |
| concepts/enums | 1 |

## Dataset Format

```json
{
  "instruction": "Write a Hemlock program that tests arithmetic operations.",
  "output": "```hemlock\nlet x = 5 + 3;\nprint(x);  // 8\n```",
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
