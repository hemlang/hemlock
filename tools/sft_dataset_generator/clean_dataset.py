#!/usr/bin/env python3
"""
SFT Dataset Cleaner

Uses Claude API to review and clean SFT dataset entries.
For each entry, Claude decides to: DROP, KEEP, or REPHRASE.

Usage:
    export ANTHROPIC_API_KEY="your-key"
    python clean_dataset.py [--input INPUT] [--output OUTPUT] [--dry-run]
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path
from dataclasses import dataclass
from enum import Enum

try:
    import anthropic
except ImportError:
    print("Please install anthropic: pip install anthropic")
    sys.exit(1)


class Decision(Enum):
    DROP = "drop"
    KEEP = "keep"
    REPHRASE = "rephrase"


@dataclass
class ReviewResult:
    decision: Decision
    reason: str
    instruction: str | None = None  # Rephrased instruction (if REPHRASE)
    output: str | None = None  # Rephrased output (if REPHRASE)


REVIEW_PROMPT = """You are reviewing entries for a Supervised Fine-Tuning (SFT) dataset for the Hemlock programming language. Your job is to ensure high-quality training data.

For each entry, decide one of:
- **DROP**: Remove this entry (low quality, duplicate info, confusing, incorrect, or not useful for training)
- **KEEP**: Keep this entry as-is (good quality, clear instruction, correct output)
- **REPHRASE**: Keep but improve the instruction and/or output (fix grammar, clarify wording, clean up formatting)

## Quality Criteria

**DROP if:**
- Instruction is confusing or grammatically broken (e.g., "How does don'ts work")
- Output is incomplete, incorrect, or doesn't match the instruction
- Entry is too trivial (e.g., single line of obvious code)
- Instruction contains artifacts like "1.", list numbers, or broken formatting
- The code example has errors or doesn't demonstrate the claimed concept
- Duplicate or near-duplicate of common patterns already well-covered

**KEEP if:**
- Instruction is clear and grammatically correct
- Output correctly addresses the instruction
- Code examples are valid Hemlock and demonstrate the concept
- Entry teaches something useful about Hemlock

**REPHRASE if:**
- Good content but instruction wording is awkward
- Minor formatting issues in output
- Could be clearer with small edits
- Instruction is too generic and could be more specific

## Response Format

Respond with valid JSON only:

```json
{
  "decision": "DROP" | "KEEP" | "REPHRASE",
  "reason": "Brief explanation of your decision",
  "instruction": "Rephrased instruction (only if REPHRASE, otherwise null)",
  "output": "Rephrased output (only if REPHRASE and output needs changes, otherwise null)"
}
```

## Entry to Review

**Instruction:**
{instruction}

**Output:**
{output}

**Category:** {category}
**Source:** {source}
"""


class DatasetCleaner:
    def __init__(self, api_key: str, model: str = "claude-sonnet-4-20250514"):
        self.client = anthropic.Anthropic(api_key=api_key)
        self.model = model
        self.stats = {"total": 0, "kept": 0, "dropped": 0, "rephrased": 0, "errors": 0}

    def review_entry(self, entry: dict) -> ReviewResult:
        """Send entry to Claude for review."""
        prompt = REVIEW_PROMPT.format(
            instruction=entry.get("instruction", ""),
            output=entry.get("output", ""),
            category=entry.get("category", "unknown"),
            source=entry.get("source", "unknown"),
        )

        try:
            response = self.client.messages.create(
                model=self.model,
                max_tokens=2048,
                messages=[{"role": "user", "content": prompt}],
            )

            # Parse response
            content = response.content[0].text.strip()

            # Extract JSON from response (handle markdown code blocks)
            if "```json" in content:
                content = content.split("```json")[1].split("```")[0].strip()
            elif "```" in content:
                content = content.split("```")[1].split("```")[0].strip()

            result = json.loads(content)

            decision = Decision(result["decision"].lower())

            return ReviewResult(
                decision=decision,
                reason=result.get("reason", ""),
                instruction=result.get("instruction"),
                output=result.get("output"),
            )

        except json.JSONDecodeError as e:
            print(f"  Warning: Failed to parse JSON response: {e}")
            # Default to KEEP if we can't parse
            return ReviewResult(decision=Decision.KEEP, reason="Parse error - keeping by default")
        except Exception as e:
            print(f"  Warning: API error: {e}")
            return ReviewResult(decision=Decision.KEEP, reason=f"API error: {e}")

    def clean_dataset(
        self,
        input_path: Path,
        output_path: Path,
        dry_run: bool = False,
        limit: int | None = None,
        delay: float = 0.5,
    ) -> None:
        """Process the dataset and write cleaned version."""

        # Read input
        print(f"Reading {input_path}...")
        entries = []
        with open(input_path, "r", encoding="utf-8") as f:
            for line in f:
                if line.strip():
                    entries.append(json.loads(line))

        print(f"Loaded {len(entries)} entries")

        if limit:
            entries = entries[:limit]
            print(f"Processing first {limit} entries (--limit)")

        # Process entries
        cleaned_entries = []
        dropped_entries = []

        for i, entry in enumerate(entries):
            self.stats["total"] += 1

            print(f"\n[{i+1}/{len(entries)}] {entry.get('category', 'unknown')}")
            print(f"  Instruction: {entry.get('instruction', '')[:60]}...")

            if dry_run:
                # In dry run, just keep everything
                cleaned_entries.append(entry)
                self.stats["kept"] += 1
                continue

            # Review with Claude
            result = self.review_entry(entry)

            print(f"  Decision: {result.decision.value.upper()}")
            print(f"  Reason: {result.reason[:80]}...")

            if result.decision == Decision.DROP:
                self.stats["dropped"] += 1
                dropped_entries.append({"entry": entry, "reason": result.reason})

            elif result.decision == Decision.KEEP:
                self.stats["kept"] += 1
                cleaned_entries.append(entry)

            elif result.decision == Decision.REPHRASE:
                self.stats["rephrased"] += 1
                # Apply rephrasing
                new_entry = entry.copy()
                if result.instruction:
                    new_entry["instruction"] = result.instruction
                if result.output:
                    new_entry["output"] = result.output
                cleaned_entries.append(new_entry)

            # Rate limiting
            time.sleep(delay)

        # Write outputs
        print(f"\nWriting {len(cleaned_entries)} entries to {output_path}...")
        with open(output_path, "w", encoding="utf-8") as f:
            for entry in cleaned_entries:
                f.write(json.dumps(entry, ensure_ascii=False) + "\n")

        # Write dropped entries log
        dropped_path = output_path.with_suffix(".dropped.jsonl")
        if dropped_entries:
            print(f"Writing {len(dropped_entries)} dropped entries to {dropped_path}...")
            with open(dropped_path, "w", encoding="utf-8") as f:
                for item in dropped_entries:
                    f.write(json.dumps(item, ensure_ascii=False) + "\n")

        # Print stats
        print("\n" + "=" * 50)
        print("CLEANING COMPLETE")
        print("=" * 50)
        print(f"Total processed: {self.stats['total']}")
        print(f"Kept as-is:      {self.stats['kept']}")
        print(f"Rephrased:       {self.stats['rephrased']}")
        print(f"Dropped:         {self.stats['dropped']}")
        print(f"Errors:          {self.stats['errors']}")
        print(f"\nOutput: {output_path}")
        if dropped_entries:
            print(f"Dropped log: {dropped_path}")


def main():
    parser = argparse.ArgumentParser(description="Clean SFT dataset using Claude API")
    parser.add_argument(
        "--input",
        "-i",
        type=Path,
        default=Path(__file__).parent / "output" / "hemlock_sft_dataset.jsonl",
        help="Input JSONL file",
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=Path(__file__).parent / "output" / "hemlock_sft_dataset_cleaned.jsonl",
        help="Output JSONL file",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Don't call API, just read and write",
    )
    parser.add_argument(
        "--limit",
        "-n",
        type=int,
        default=None,
        help="Only process first N entries",
    )
    parser.add_argument(
        "--delay",
        type=float,
        default=0.5,
        help="Delay between API calls in seconds (default: 0.5)",
    )
    parser.add_argument(
        "--model",
        type=str,
        default="claude-sonnet-4-20250514",
        help="Claude model to use",
    )

    args = parser.parse_args()

    # Check API key
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key and not args.dry_run:
        print("Error: ANTHROPIC_API_KEY environment variable not set")
        print("Set it with: export ANTHROPIC_API_KEY='your-key'")
        sys.exit(1)

    # Validate input
    if not args.input.exists():
        print(f"Error: Input file not found: {args.input}")
        sys.exit(1)

    # Run cleaner
    cleaner = DatasetCleaner(api_key=api_key or "", model=args.model)
    cleaner.clean_dataset(
        input_path=args.input,
        output_path=args.output,
        dry_run=args.dry_run,
        limit=args.limit,
        delay=args.delay,
    )


if __name__ == "__main__":
    main()
