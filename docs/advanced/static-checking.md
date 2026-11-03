# Static Checking (`hemlock check`)

`hemlock check` runs the compiler's static analysis passes over source files
**without executing them or generating code**: syntax, name resolution, lint,
type checking, and borrow checking. Unlike `hemlockc --check`, which stops at the first
failing pass, `hemlock check` collects every diagnostic from every pass and
reports them together, in source order — and can emit them as JSON with a
stable schema for editors, CI, and AI coding agents.

## Usage

```bash
hemlock check <FILE>...           # Human-readable diagnostics
hemlock check --json <FILE>...    # Machine-readable diagnostics
```

Options:

| Flag | Effect |
|------|--------|
| `--json` | Output a single JSON object instead of text |
| `--strict-types` | Strict type checking (warn on implicit `any`) |
| `--borrow-strict` | Strict borrow checking (move tracking + leak detection) |
| `--lint-strict` | Strict lint (also flag unused variables) |
| `--no-lint` | Disable the lint pass |
| `--deny-warnings` | Exit 1 when any warning is reported |

Exit codes:

| Code | Meaning |
|------|---------|
| 0 | No errors (warnings do not fail the check, unless `--deny-warnings`) |
| 1 | One or more errors found, or any warning under `--deny-warnings` |
| 2 | Usage or I/O error (unknown flag, unreadable file) |

## What runs

The passes mirror the compiler (`hemlockc`) in its order, so `hemlock check`
reports what a compile would report:

1. **Parse** — syntax errors, with panic-mode recovery so *multiple* errors
   are reported per run instead of only the first.
2. **Name resolution** — identifiers that no binding in scope explains
   (see [Name resolution](#name-resolution) below).
3. **Lint** — unreachable code, dead branches, self-assignment,
   modulo-by-zero, duplicate fields/cases (see [Static Lint](lint.md)).
   Runs on the source as written, before optimization.
4. **Type check** — static checking of annotations, function signatures,
   object literals against `define` types (see [Type System](../reference/type-system.md)).
5. **Borrow check** — use-after-free, double-free, free-in-loop
   (see [Borrow Checker](borrow-checker.md)).

If the file has syntax errors, the later passes are skipped (their input
would be an incomplete AST) — but all syntax errors found are still reported.

Name resolution follows imports, so it reports diagnostics in modules the
named files pull in — including a syntax error in one, under pass `"parse"`.
The other passes analyze only the named files; an imported module is linted,
type checked and borrow checked when it is checked or compiled in its own
right.

## Name resolution

The name pass walks the entry file **and every module it imports,
transitively**, and reports each identifier that nothing in scope explains:
not a builtin, not a module-level declaration, not an import, not a
parameter, not a loop or pattern binding, not a catch parameter.

```
$ hemlock check repro.hml
repro.hml:2:20: error: undefined name 'sleep' (did you mean to import it from @stdlib/time?) [names]
1 error, 0 warnings (1 file checked)
```

It is an **error**, not a warning: Hemlock has no implicit declaration, so a
name nothing defines throws `Undefined variable` the moment control reaches
it — which may be weeks after deploy, or never. Assigning to an undeclared
name is reported the same way, for the same reason.

The same pass gates `hemlockc` and runs at interpreter startup, so all three
tools agree about what exists. See
[Name Resolution](../reference/name-resolution.md) for what it checks, what
it deliberately does not, and the `--no-name-check` escape hatch.

The hint is best-effort, in this order: a module the file already imports
exports that name; a stdlib module exports it; something spelled almost the
same is in scope. Where several modules export the name they are all collected
and sorted, so the message does not depend on the order they were discovered
in — see [Name Resolution](../reference/name-resolution.md#hints).

## Text output

One diagnostic per line, in a `grep`/editor-friendly format, followed by a
summary line:

```
$ hemlock check app.hml
app.hml:2:1: error: Expect ';' after variable declaration (at 'let') [parse]
app.hml:14: warning: double free: 'p' was already freed (line 9) [borrow]
1 error, 1 warning (1 file checked)
```

The format is `<file>:<line>:<column>: <severity>: <message> [<pass>]`.
The column is omitted when unknown. All diagnostics go to **stdout**;
stderr is reserved for I/O failures (e.g. an unreadable file).

## JSON output

`--json` prints a single object with a stable schema (`version` is bumped on
breaking changes):

```json
{
  "version": 1,
  "files": 1,
  "errors": 1,
  "warnings": 0,
  "diagnostics": [
    {
      "file": "app.hml",
      "line": 1,
      "column": 14,
      "end_column": 15,
      "severity": "error",
      "pass": "types",
      "message": "cannot initialize 'x' of type 'i32' with 'string'"
    }
  ]
}
```

- `line` and `column` are 1-based; `column: 0` means the column is unknown.
- `severity` is `"error"` or `"warning"`.
- `pass` is one of `"parse"`, `"names"`, `"lint"`, `"types"`, `"borrow"`.
- `diagnostics` is ordered by file (in argument order), then line, then column.

## Why not just run the file?

Running a script executes its side effects and stops at the first runtime
error. `hemlock check` is safe to run on any code — including code you have
not reviewed — and reports *all* statically detectable problems at once,
which makes it the right feedback loop for editors, CI pipelines, and
AI-assisted development:

```bash
# CI: fail the build on static errors
hemlock check src/*.hml

# Agent loop: machine-readable diagnostics
hemlock check --json src/main.hml
```

## See Also

- [Name Resolution](../reference/name-resolution.md) — the name pass in detail
- [Static Lint](lint.md) — the lint pass in detail
- [Borrow Checker](borrow-checker.md) — the ownership analysis in detail
- [Type System](../reference/type-system.md) — what the type checker enforces
- [Code Formatting](code-formatting.md) — `hemlock format`
