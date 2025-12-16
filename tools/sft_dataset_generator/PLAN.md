# SFT Dataset Generator Plan

## Overview

Generate a Supervised Fine-Tuning (SFT) dataset from Hemlock repository content to train LLMs on the Hemlock programming language. The dataset will be HuggingFace-compatible in JSONL format.

---

## Output Format

### HuggingFace-Compatible Schema

```json
{
  "instruction": "What does this Hemlock program output?",
  "input": "let x = 5 + 3;\nprint(x);",
  "output": "8",
  "category": "arithmetic",
  "source": "tests/parity/language/arithmetic.hml",
  "difficulty": "basic"
}
```

**Fields:**
- `instruction` (required): The prompt/task description
- `input` (optional): Code or context provided to the model
- `output` (required): Expected response
- `category`: Topic categorization for filtering
- `source`: Original file path for traceability
- `difficulty`: basic | intermediate | advanced

---

## Data Sources & Extraction Strategies

### 1. Parity Tests (71 pairs) - **Highest Quality**

**Source:** `tests/parity/**/*.hml` + `tests/parity/**/*.expected`

**Strategy:** Direct mapping of code → expected output

**Instruction Templates:**
- "What is the output of this Hemlock program?"
- "Predict the output of the following Hemlock code:"
- "Execute this Hemlock code and show the output:"

**Example Entry:**
```json
{
  "instruction": "What does this Hemlock program output?",
  "input": "print(5 + 3);\nprint(10 - 4);",
  "output": "8\n6",
  "category": "arithmetic",
  "source": "tests/parity/language/arithmetic.hml"
}
```

### 2. Regular Tests with Expected Output (96+ pairs)

**Source:** `tests/**/*.hml` + `tests/**/*.expected` (excluding parity)

**Strategy:** Same as parity tests - code → output mapping

**Categories to extract:**
- `compiler/` - Compiler-specific tests
- `objects/` - Object manipulation
- `arrays/` - Array operations
- `strings/` - String methods
- `async/` - Async/concurrency
- `exceptions/` - Error handling
- And 50+ other categories

### 3. Documentation Code Blocks (~500+ snippets)

**Source:** `docs/**/*.md` + `stdlib/docs/**/*.md`

**Strategy:** Extract fenced code blocks with surrounding context

**Instruction Templates:**
- "Explain what this Hemlock code does:"
- "Write Hemlock code to [description from context]"
- "How do you [task] in Hemlock?"

**Example Entry:**
```json
{
  "instruction": "How do you create and use a HashMap in Hemlock?",
  "input": "",
  "output": "```hemlock\nimport { HashMap } from \"@stdlib/collections\";\n\nlet map = HashMap();\nmap.set(\"key\", \"value\");\nlet val = map.get(\"key\");\nprint(val);  // \"value\"\n```",
  "category": "stdlib/collections",
  "source": "stdlib/docs/collections.md"
}
```

### 4. Example Programs (26 files)

**Source:** `examples/*.hml`

**Strategy:** Generate multiple instruction types per example

**Instruction Templates:**
- "Explain this Hemlock program:"
- "What does this Hemlock HTTP client do?"
- "Write a Hemlock program that [inferred from filename/content]"

### 5. Method Documentation → Usage Examples

**Source:** String methods (19) + Array methods (18) from `docs/reference/`

**Strategy:** Generate method-specific instruction pairs

**Example Entry:**
```json
{
  "instruction": "How do you split a string in Hemlock?",
  "input": "",
  "output": "Use the `split()` method:\n\n```hemlock\nlet csv = \"a,b,c\";\nlet parts = csv.split(\",\");\nprint(parts[0]);  // \"a\"\nprint(parts[1]);  // \"b\"\nprint(parts[2]);  // \"c\"\n```",
  "category": "string_methods",
  "source": "docs/reference/string-api.md"
}
```

### 6. Concept Explanations from CLAUDE.md

**Source:** `CLAUDE.md` quick reference sections

**Strategy:** Extract type info, syntax rules, operators

**Example Entry:**
```json
{
  "instruction": "What integer types are available in Hemlock?",
  "input": "",
  "output": "Hemlock provides signed and unsigned integers:\n\n**Signed:** i8, i16, i32, i64\n**Unsigned:** u8, u16, u32, u64\n\nType aliases: `integer` (i32), `byte` (u8)\n\nLiterals auto-detect size: `42` → i32, `5000000000` → i64 (exceeds i32 max)",
  "category": "types",
  "source": "CLAUDE.md"
}
```

### 7. Error Handling Patterns

**Source:** `tests/exceptions/*.hml`

**Strategy:** Generate try/catch/throw examples

### 8. Stdlib Module Usage

**Source:** `tests/stdlib_*/*.hml` + `stdlib/docs/*.md`

**Strategy:** Import + usage pattern pairs

---

## Instruction Template Categories

### Category 1: Output Prediction
- "What does this Hemlock program output?"
- "Predict the output:"
- "What will be printed?"

### Category 2: Code Explanation
- "Explain what this code does:"
- "What is the purpose of this function?"
- "Describe the behavior of this program:"

### Category 3: Code Generation
- "Write Hemlock code to..."
- "Implement a function that..."
- "Show how to..."

### Category 4: Concept Questions
- "What is [concept] in Hemlock?"
- "How does [feature] work in Hemlock?"
- "What's the difference between [A] and [B]?"

### Category 5: Debugging/Fix
- "What's wrong with this code?"
- "Fix the bug in this program:"
- "Why does this code fail?"

### Category 6: Translation/Conversion
- "Convert this [language] code to Hemlock:"
- "How would you write this in Hemlock?"

---

## Implementation Plan

### Phase 1: Core Infrastructure
```
tools/sft_dataset_generator/
├── generate_dataset.py      # Main entry point
├── extractors/
│   ├── __init__.py
│   ├── parity_extractor.py  # Extract parity test pairs
│   ├── test_extractor.py    # Extract regular test pairs
│   ├── doc_extractor.py     # Extract from markdown docs
│   ├── example_extractor.py # Extract from examples
│   └── stdlib_extractor.py  # Extract stdlib usage
├── templates/
│   └── instructions.py      # Instruction template library
├── utils/
│   ├── __init__.py
│   ├── markdown_parser.py   # Parse markdown code blocks
│   └── hemlock_parser.py    # Parse .hml files for comments
└── output/
    └── (generated datasets)
```

### Phase 2: Extractors (Priority Order)

1. **Parity Extractor** - Highest quality, direct code→output
2. **Test Extractor** - More volume, same pattern
3. **Doc Extractor** - Rich explanations and examples
4. **Example Extractor** - Complete program understanding
5. **Stdlib Extractor** - API usage patterns

### Phase 3: Output Generation

- Generate JSONL file (one JSON object per line)
- Generate statistics/summary
- Validate all entries
- Optional: HuggingFace `datasets` format

---

## Expected Dataset Size

| Source | Estimated Entries |
|--------|-------------------|
| Parity tests | 200-300 |
| Regular tests | 400-600 |
| Documentation | 300-500 |
| Examples | 100-150 |
| Stdlib docs | 200-300 |
| Concept Q&A | 100-150 |
| **Total** | **1,300-2,000** |

---

## Quality Controls

1. **Deduplication** - Remove near-duplicate entries
2. **Validation** - Ensure all code snippets are valid Hemlock
3. **Balance** - Ensure coverage across all categories
4. **Length limits** - Filter very long or very short entries
5. **Source tracking** - Maintain traceability to original files

---

## HuggingFace Upload Format

Final output will be:
1. `hemlock_sft_dataset.jsonl` - Main dataset file
2. `dataset_info.json` - Metadata for HuggingFace
3. `README.md` - Dataset card for HuggingFace

Optional: Generate `train.jsonl`, `validation.jsonl` split (90/10)

---

## Configuration Options

```python
CONFIG = {
    "output_dir": "output/",
    "include_sources": ["parity", "tests", "docs", "examples", "stdlib"],
    "instruction_templates": "varied",  # or "single"
    "difficulty_labeling": True,
    "min_code_length": 10,
    "max_code_length": 2000,
    "train_split": 0.9,
    "seed": 42
}
```

---

## Open Questions for Review

1. **Instruction diversity**: Should we use varied instruction templates per entry, or stick to consistent phrasing for each category?

2. **Code-only vs Code+Explanation**: For documentation extracts, should output be just code, or code with explanation?

3. **Difficulty labeling**: Should we auto-label difficulty based on code complexity metrics?

4. **Multi-turn format**: Should we include any multi-turn conversation examples (e.g., follow-up questions)?

5. **Language mixing**: Should we include "Convert from Python/JavaScript to Hemlock" style entries?

6. **Negative examples**: Should we include "What's wrong with this code?" entries using intentionally buggy code?

---

## Next Steps

After plan approval:
1. Implement core infrastructure and parity extractor
2. Generate initial dataset from parity tests
3. Add remaining extractors incrementally
4. Generate full dataset
5. Create HuggingFace upload files
6. Commit and push
