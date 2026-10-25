#!/usr/bin/env python3
"""Apply a mutant to a PlusCal model and re-translate it.

    mutate.py MUTANT.mut MODULE.tla OUTDIR TLA2TOOLS_JAR

A mutant plants one known bug in the PlusCal source of a model. Each is listed
in proofs.txt with expected verdict `fail`, so the run breaks if the model
stops catching it: that is how a model that has silently become vacuous is
noticed.

Mutant file format: `#` comment lines, then one or more edits

    --- old
    <exact PlusCal text>
    +++ new
    <replacement text>

Every occurrence of the old text in the PlusCal part (before BEGIN
TRANSLATION) is replaced; it must occur at least once.
"""
import os
import subprocess
import sys


def parse(path):
    edits, cur = [], None          # cur: the list the next text line goes to
    for line in open(path).read().split("\n"):
        if line == "--- old":
            edits.append(([], []))
            cur = edits[-1][0]
        elif line == "+++ new":
            if cur is not edits[-1][0]:
                sys.exit(f"{path}: '+++ new' without a preceding '--- old'")
            cur = edits[-1][1]
        elif cur is not None:
            cur.append(line)
        elif line.strip() and not line.startswith("#"):
            sys.exit(f"{path}: text before the first '--- old'")
    result = [("\n".join(o).rstrip("\n"), "\n".join(n).rstrip("\n")) for o, n in edits]
    if not result or any(not o for o, _ in result):
        sys.exit(f"{path}: no edits, or an edit with empty old text")
    return result


def main():
    mutant, module, outdir, jar = sys.argv[1:4] + [os.path.abspath(sys.argv[4])]
    src = open(module).read()
    marker = "\\* BEGIN TRANSLATION"
    pcal, _ = src.split(marker, 1)
    for old, new in parse(mutant):
        if old not in pcal:
            sys.exit(f"{mutant}: text not found in {module}:\n{old}")
        pcal = pcal.replace(old, new)
    out = os.path.join(outdir, os.path.basename(module))
    with open(out, "w") as f:
        f.write(pcal + marker + "\n\\* END TRANSLATION\n" + "=" * 78 + "\n")
    r = subprocess.run(["java", "-cp", jar, "pcal.trans", "-nocfg", os.path.basename(out)],
                       cwd=outdir, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"pcal.trans failed on mutant {mutant}:\n{r.stdout}{r.stderr}")


if __name__ == "__main__":
    main()
