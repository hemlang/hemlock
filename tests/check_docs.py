#!/usr/bin/env python3
"""Documentation consistency checks for Hemlock.

The checks are intentionally lightweight and dependency-free so they can run in
CI and in local development environments without a Markdown toolchain.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
REFERENCE_LINK_DEF = re.compile(r"^\s*\[[^\]]+\]:\s+(\S+)", re.MULTILINE)
AUTOLINK = re.compile(r"<((?:\.\.?/|/)[^>]+)>")
EXTERNAL_SCHEMES = (
    "http://",
    "https://",
    "mailto:",
    "tel:",
    "ftp://",
    "urn:",
)


def iter_markdown_files() -> list[Path]:
    ignored_parts = {".git", "build", "node_modules", "vendor"}
    return sorted(
        path
        for path in ROOT.rglob("*.md")
        if not ignored_parts.intersection(path.relative_to(ROOT).parts)
    )


def strip_fenced_code(text: str) -> str:
    """Remove fenced code blocks while preserving line numbers."""
    stripped: list[str] = []
    in_fence = False
    for line in text.splitlines(keepends=True):
        if line.lstrip().startswith("```") or line.lstrip().startswith("~~~"):
            in_fence = not in_fence
            stripped.append("\n")
        elif in_fence:
            stripped.append("\n")
        else:
            stripped.append(line)
    return "".join(stripped)


def link_targets(text: str) -> list[tuple[str, int]]:
    targets: list[tuple[str, int]] = []
    for regex in (MARKDOWN_LINK, REFERENCE_LINK_DEF, AUTOLINK):
        for match in regex.finditer(text):
            targets.append((match.group(1), text.count("\n", 0, match.start()) + 1))
    return targets


def is_external(target: str) -> bool:
    return target.startswith(EXTERNAL_SCHEMES) or target.startswith("#")


def resolve_target(source: Path, target: str) -> Path | None:
    clean = target.strip().strip("<>")
    if not clean or is_external(clean):
        return None
    if clean.startswith("//"):
        return None
    path_part = clean.split("#", 1)[0]
    if not path_part:
        return None
    path_part = unquote(path_part)
    if path_part.startswith("/"):
        return ROOT / path_part.lstrip("/")
    return (source.parent / path_part).resolve()


def check_links() -> list[str]:
    errors: list[str] = []
    for md in iter_markdown_files():
        rel = md.relative_to(ROOT)
        text = strip_fenced_code(md.read_text(encoding="utf-8"))
        for target, line in link_targets(text):
            resolved = resolve_target(md, target)
            if resolved is None:
                continue
            if not resolved.exists():
                errors.append(f"{rel}:{line}: broken link target: {target}")
    return errors


def check_stdlib_docs() -> list[str]:
    errors: list[str] = []
    modules = {path.stem for path in (ROOT / "stdlib").glob("*.hml")}
    docs = {path.stem for path in (ROOT / "stdlib" / "docs").glob("*.md")}

    for module in sorted(modules - docs):
        errors.append(f"stdlib/{module}.hml has no stdlib/docs/{module}.md")
    for doc in sorted(docs - modules):
        errors.append(f"stdlib/docs/{doc}.md has no stdlib/{doc}.hml")

    overview = (ROOT / "docs" / "reference" / "stdlib-overview.md").read_text(
        encoding="utf-8"
    )
    for module in sorted(modules):
        if f"../../stdlib/docs/{module}.md" not in overview:
            errors.append(
                f"docs/reference/stdlib-overview.md does not link stdlib/docs/{module}.md"
            )
    return errors


def main() -> int:
    errors = check_stdlib_docs() + check_links()
    if errors:
        print("Documentation audit failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    md_count = len(iter_markdown_files())
    module_count = len(list((ROOT / "stdlib").glob("*.hml")))
    print(
        f"Documentation audit passed ({md_count} Markdown files, "
        f"{module_count} stdlib modules)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
