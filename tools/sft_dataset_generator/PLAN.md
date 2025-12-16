# SFT Dataset Generator Plan

## Overview

Generate a Supervised Fine-Tuning (SFT) dataset from Hemlock repository content to train LLMs on the Hemlock programming language. The dataset will be HuggingFace-compatible in JSONL format.

---

## Output Format

### Simplified Schema

```json
{
  "instruction": "What does this Hemlock program output?\n\n```hemlock\nlet x = 5 + 3;\nprint(x);\n```",
  "output": "8"
}
```

**Fields:**
- `instruction` (required): The prompt/task with any code included inline
- `output` (required): Expected response

Additional metadata fields for filtering (not used in training):
- `category`: Topic categorization
- `source`: Original file path for traceability

---

## Data Sources & Extraction Strategies

### 1. Parity Tests (71 pairs) - **Highest Quality**

**Source:** `tests/parity/**/*.hml` + `tests/parity/**/*.expected`

**Strategy:** Direct mapping of code → expected output

**Instruction phrasing variations:**
- "What is the output of this Hemlock program?"
- "What does the following Hemlock code print?"
- "Run this Hemlock code and show the output:"

**Example Entry:**
```json
{
  "instruction": "What does this Hemlock program output?\n\n```hemlock\nprint(5 + 3);\nprint(10 - 4);\n```",
  "output": "8\n6"
}
```

### 2. Regular Tests with Expected Output

**Source:** `tests/**/*.hml` + `tests/**/*.expected` (excluding parity)

**Strategy:** Same as parity tests - code → output mapping

### 3. Documentation Code Blocks

**Source:** `docs/**/*.md` + `stdlib/docs/**/*.md`

**Strategy:** Extract code blocks with surrounding context

For questions about the language → include explanations with code
For code example requests → code-only output

**Example Entry:**
```json
{
  "instruction": "How do you create and use a HashMap in Hemlock?",
  "output": "Use the HashMap from the collections standard library module:\n\n```hemlock\nimport { HashMap } from \"@stdlib/collections\";\n\nlet map = HashMap();\nmap.set(\"key\", \"value\");\nlet val = map.get(\"key\");\nprint(val);  // \"value\"\n```\n\nHashMap provides O(1) average lookup, insertion, and deletion."
}
```

### 4. Example Programs (26 files)

**Source:** `examples/*.hml`

**Strategy:** Generate explanation-style entries

**Example Entry:**
```json
{
  "instruction": "Write a Hemlock program that demonstrates memory management with alloc and free.",
  "output": "```hemlock\n// Allocate 64 bytes\nlet p = alloc(64);\n\n// Initialize memory\nmemset(p, 0, 64);\n\n// Use the memory...\n\n// Free when done\nfree(p);\n```"
}
```

### 5. Method Documentation

**Source:** String methods (19) + Array methods (18) from docs

**Strategy:** Generate method-specific Q&A pairs

### 6. Concept Explanations

**Source:** `CLAUDE.md` and `docs/` reference sections

**Strategy:** Extract type info, syntax rules, operators into Q&A format

---

## Instruction Phrasing Categories

Vary phrasing within each category to increase diversity:

### Output Prediction
- "What does this Hemlock program output?"
- "What does the following Hemlock code print?"
- "What is the result of running this Hemlock code?"

### Code Explanation
- "Explain what this Hemlock code does:"
- "What is the purpose of this Hemlock function?"
- "Describe the behavior of this Hemlock program:"

### Code Generation
- "Write Hemlock code to..."
- "Show how to [task] in Hemlock"
- "Provide a Hemlock example that..."

### Concept Questions
- "What is [concept] in Hemlock?"
- "How does [feature] work in Hemlock?"
- "Explain [feature] in Hemlock"

---

## Implementation

Single Python script: `generate_dataset.py`

### Extractors (in order):
1. Parity test extractor
2. Regular test extractor
3. Documentation extractor
4. Example extractor

### Output:
- `hemlock_sft_dataset.jsonl` - Main dataset
- `README.md` - Dataset card for HuggingFace

---

## Expected Dataset Size

| Source | Estimated Entries |
|--------|-------------------|
| Parity tests | 200-300 |
| Regular tests | 400-600 |
| Documentation | 300-500 |
| Examples | 100-150 |
| **Total** | **~1,000-1,500** |
