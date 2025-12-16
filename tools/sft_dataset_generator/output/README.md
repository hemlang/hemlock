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

This dataset contains instruction/output pairs for training language models on the Hemlock programming language. It covers:

- Language syntax and semantics
- Standard library usage
- Code examples and explanations
- Concept Q&A

## Dataset Statistics

- **Total entries:** 1767
- **Format:** JSONL

### Entries by Category

| Category | Count |
|----------|-------|
| docs | 1550 |
| tests/compiler | 49 |
| parity/language | 40 |
| examples | 23 |
| parity/builtins | 19 |
| tests/exceptions | 12 |
| examples/explanation | 9 |
| tests/enums | 7 |
| parity/modules | 7 |
| tests/typed_arrays | 6 |
| tests/arithmetic | 6 |
| tests/arrays | 6 |
| tests/async | 6 |
| parity/methods | 4 |
| concepts/types | 3 |
| concepts/memory | 2 |
| tests/memory | 2 |
| tests/functions | 2 |
| concepts/operators | 1 |
| concepts/functions | 1 |
| concepts/io | 1 |
| concepts/defer | 1 |
| concepts/strings | 1 |
| concepts/enums | 1 |
| concepts/modules | 1 |
| concepts/errors | 1 |
| concepts/objects | 1 |
| concepts/async | 1 |
| concepts/ffi | 1 |
| concepts/arrays | 1 |
| tests/objects | 1 |
| concepts/control-flow | 1 |

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
