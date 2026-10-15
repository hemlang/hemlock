#!/usr/bin/env python3
"""Generate the stdlib API index (docs/reference/stdlib-api-index.md).

Extracts every exported symbol from stdlib/*.hml into a one-line-per-symbol
index. The goal is a compact, complete map of the entire stdlib API surface
that fits in a small context window — for AI assistants, editors, and quick
human reference — generated from source so it cannot drift.

Extraction rules (stdlib exports only ever use `export fn` / `export let`):

- `export fn name(params): ret`   -> "fn name(params): ret", signature taken
  verbatim from source (multi-line signatures are joined). The description is
  the first line of the `//` comment block immediately above the export, or a
  trailing `//` comment on the same line.
- `export let name = __builtin;`  -> a native re-export; the .hml carries no
  signature, so the parameter list is taken from the module's
  stdlib/docs/<module>.md heading (`### name(params)`) when one exists.
  Marked [native].
- `export let name = <literal>;`  -> "let name = <literal>" (constants keep
  their value).
- `export let name = <expr>;`     -> "let name".

Like tests/check_docs.py, this is dependency-free stdlib-python so it runs
anywhere.

Usage:
    python3 tests/gen_stdlib_api.py           # (re)write the index
    python3 tests/gen_stdlib_api.py --check   # exit 1 if the index is stale
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STDLIB = ROOT / "stdlib"
OUTPUT = ROOT / "docs" / "reference" / "stdlib-api-index.md"

EXPORT_FN = re.compile(r"^export\s+(async\s+)?fn\s+([A-Za-z_][A-Za-z_0-9]*)\s*\(")
EXPORT_LET = re.compile(r"^export\s+let\s+([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.+?);\s*(?://\s*(.*))?$")
# `### name(params)`, `### name(params): ret`, with or without backticks
DOC_FN_HEADING = re.compile(
    r"^###\s+`?([A-Za-z_][A-Za-z_0-9]*)\(([^)]*)\)(\s*:\s*[^`]+?)?`?\s*$"
)
# `### NAME` — a constant section (ALL_CAPS so prose headings don't match)
DOC_CONST_HEADING = re.compile(r"^###\s+`?([A-Z_][A-Z_0-9]*)`?\s*$")
# Trailing source comment that itself spells the signature, e.g.
# `// bswap16(val: u16) -> u16  - Reverse bytes of 16-bit value`
COMMENT_SIG = re.compile(
    r"^([A-Za-z_][A-Za-z_0-9]*)\(([^)]*)\)\s*(?:->\s*([^-]+?))?\s*(?:[-–—]\s*(.*))?$"
)
LITERAL = re.compile(r'^(-?\d[\d_]*(\.\d+)?|"[^"]*"|true|false|null)$')
ALL_CAPS = re.compile(r"^[A-Z][A-Z_0-9]*$")
SEPARATOR_COMMENT = re.compile(r"^//\s*[=\-]{3,}")


def doc_signatures(module: str) -> tuple[dict[str, str], set[str]]:
    """Extract (name -> signature-suffix, constant-names) from docs headings.

    The signature suffix is everything after the name: "(params)" or
    "(params): ret".
    """
    path = STDLIB / "docs" / f"{module}.md"
    sigs: dict[str, str] = {}
    consts: set[str] = set()
    if not path.exists():
        return sigs, consts
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        m = DOC_FN_HEADING.match(stripped)
        if m:
            name, params, ret = m.group(1), m.group(2).strip(), m.group(3)
            if name not in sigs:
                sigs[name] = f"({params}){ret.rstrip() if ret else ''}"
            continue
        m = DOC_CONST_HEADING.match(stripped)
        if m:
            consts.add(m.group(1))
    return sigs, consts


def preceding_description(lines: list[str], index: int) -> str:
    """First line of the contiguous // comment block directly above lines[index]."""
    block: list[str] = []
    i = index - 1
    while i >= 0:
        stripped = lines[i].strip()
        if stripped.startswith("//") and not SEPARATOR_COMMENT.match(stripped):
            block.append(stripped.lstrip("/").strip())
            i -= 1
        else:
            break
    if not block:
        return ""
    return block[-1]  # block was collected bottom-up; last element is the first line


def join_signature(lines: list[str], index: int) -> tuple[str, int]:
    """Join an `export fn` header across lines until its body starts.

    Returns (signature text without the `export ` prefix or body, index of the
    last consumed line).
    """
    collected = []
    depth = 0
    saw_paren = False
    i = index
    while i < len(lines):
        line = lines[i]
        # Strip trailing comments outside of strings (stdlib headers don't put
        # '//' inside default-value strings, so a plain find is sufficient).
        code = line.split("//", 1)[0].rstrip()
        collected.append(code.strip())
        depth += code.count("(") - code.count(")")
        if "(" in code:
            saw_paren = True
        if saw_paren and depth <= 0:
            break
        i += 1
    sig = " ".join(part for part in collected if part)
    sig = re.sub(r"\s+", " ", sig)
    sig = sig.removeprefix("export ").strip()
    # Cut the body: everything from the first '{' or '=>' after the params
    sig = re.split(r"\s*(?:\{|=>)", sig, 1)[0].strip()
    return sig, i


def native_entry(name: str, desc: str,
                 sigs: dict[str, str], consts: set[str]) -> tuple[str, str]:
    """Best-effort signature for `export let name = __builtin;`.

    Precedence: docs function heading > signature spelled in the trailing
    source comment > docs constant heading / ALL_CAPS naming (a constant) >
    bare "(...)".
    Returns (entry, remaining-description).
    """
    m = COMMENT_SIG.match(desc) if desc else None
    comment_spells_sig = bool(m and m.group(1) == name)
    if comment_spells_sig:
        # The comment restates the signature — keep only the prose part.
        desc = (m.group(4) or "").strip()

    if name in sigs:
        return f"fn {name}{sigs[name]}  [native]", desc

    if comment_spells_sig:
        params, ret = m.group(2).strip(), m.group(3)
        suffix = f": {ret.strip()}" if ret else ""
        return f"fn {name}({params}){suffix}  [native]", desc

    if name in consts or ALL_CAPS.match(name):
        return f"let {name}  [native]", desc

    return f"fn {name}(...)  [native]", desc


def index_module(path: Path) -> list[str]:
    module = path.stem
    lines = path.read_text(encoding="utf-8").splitlines()
    sigs, consts = doc_signatures(module)
    entries: list[str] = []

    i = 0
    while i < len(lines):
        line = lines[i]

        if EXPORT_FN.match(line):
            sig, last = join_signature(lines, i)
            desc = preceding_description(lines, i)
            entries.append(f"{sig}  // {desc}" if desc else sig)
            i = last + 1
            continue

        m = EXPORT_LET.match(line.strip())
        if m:
            name, value, trailing = m.group(1), m.group(2).strip(), m.group(3)
            desc = (trailing or "").strip() or preceding_description(lines, i)
            if value.startswith("__"):
                entry, desc = native_entry(name, desc, sigs, consts)
            elif LITERAL.match(value):
                entry = f"let {name} = {value}"
            else:
                entry = f"let {name}"
            entries.append(f"{entry}  // {desc}" if desc else entry)
        i += 1

    return entries


def generate() -> str:
    modules = sorted(STDLIB.glob("*.hml"))
    total = 0
    sections: list[str] = []
    for path in modules:
        entries = index_module(path)
        total += len(entries)
        body = "\n".join(entries)
        sections.append(f"## @stdlib/{path.stem}\n\n```\n{body}\n```\n")

    header = (
        "# Stdlib API Index\n"
        "\n"
        "<!-- GENERATED FILE - DO NOT EDIT. Regenerate with: make stdlib-api -->\n"
        "\n"
        "One line per exported symbol across the entire standard library — the\n"
        "complete importable API surface, generated from `stdlib/*.hml` by\n"
        "`tests/gen_stdlib_api.py` so it cannot drift from the code. Signatures\n"
        "are verbatim from source; `[native]` marks re-exported native builtins\n"
        "whose parameter list comes from the module's documentation.\n"
        "\n"
        f"{len(modules)} modules, {total} exported symbols. Import with\n"
        "`import { name } from \"@stdlib/<module>\";` — see\n"
        "[Standard Library Overview](stdlib-overview.md) and `stdlib/docs/` for\n"
        "full documentation.\n"
        "\n"
    )
    return header + "\n".join(sections)


def main() -> int:
    content = generate()
    if "--check" in sys.argv[1:]:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != content:
            print(
                f"{OUTPUT.relative_to(ROOT)} is out of date with stdlib/*.hml.\n"
                "Regenerate it with: make stdlib-api"
            )
            return 1
        print(f"Stdlib API index is up to date ({OUTPUT.relative_to(ROOT)}).")
        return 0

    OUTPUT.write_text(content, encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(ROOT)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
