# Specialization Coverage (`--coverage`)

The compiler's [unboxing optimization](compiler-optimizations.md) gives
numeric variables native C types (`int32_t`, `double`, ...) when it can
prove that is safe, and falls back to boxed `HmlValue` handling when it
cannot. The specialization coverage report makes that decision visible:
for every numeric variable site it says **unboxed** or **boxed**, and every
boxed site carries a stable `HC21xx` reason code and, where one helps, a
hint for unlocking the specialization.

Nothing about the optimization changes — the report only reveals what the
compiler already does, in the spirit of "explicit over implicit". Use it to
find out why a hot loop is slower than expected, or which annotation would
let the compiler specialize it.

## Usage

Both backends serve the same report:

```bash
hemlockc --coverage file.hml            # analyze and report, don't compile
hemlockc --coverage --json file.hml     # same report as JSON

hemlock check --coverage <FILE>...          # appended to the diagnostics
hemlock check --coverage --json <FILE>...   # adds a "coverage" key to the JSON
```

`hemlockc --coverage` is a static-analysis run like `--check`: it stops
before code generation and exits 0 unless the program fails to parse or
type-check.

## Reading the report

```
Specialization coverage: matmul.hml
  8 numeric sites: 2 unboxed (25%), 6 boxed

  matmul.hml:4  boxed    sum — escapes its scope (captured, stored, returned, or indexed) [HC2105]
                        hint: keep hot arithmetic in a local that is not captured, stored, or returned
  matmul.hml:5  unboxed  i: i32 (loop_counter)
  ...
```

A **site** is a variable the unboxing analysis considers: a `let` with a
primitive type annotation, an untyped `let` whose initializer is
structurally numeric (literals, identifiers, arithmetic), a `for`-loop
counter, a `while`-loop accumulator, a function parameter with a primitive
annotation, or a top-level variable that would otherwise qualify.
Declarations that are not numeric candidates (strings, arrays, objects,
function values, call-initialized untyped variables) are not counted.

The unboxed/boxed decision is ground truth for the default optimization
level: the report runs the same analysis code generation runs and reads its
results. (`-O0` disables unboxing entirely; the report always describes the
optimized build.)

## Reason codes

| Code | Meaning | Typical fix |
|------|---------|-------------|
| `HC2101` | Not specialized by the current analysis | — |
| `HC2102` | No static numeric type could be inferred | Add a numeric annotation (`: i32`) |
| `HC2103` | Top-level variables stay boxed | Move hot code into a function |
| `HC2104` | Parameters are always boxed | Copy into an annotated local at function entry |
| `HC2105` | Escapes its scope (captured, stored, returned, or indexed) | Keep hot arithmetic in a non-escaping local |
| `HC2106` | Initializer is not statically analyzable | Initialize with a literal or arithmetic expression |
| `HC2107` | A later assignment is not statically numeric | Keep assignments numeric, or annotate |
| `HC2108` | Loop shape too complex to specialize | Constant-integer init, simple comparison, constant step |
| `HC2109` | Runes keep their type tag | Expected: unboxing a rune would change `typeof`/printing |
| `HC2110` | Inferred type has no unboxed form | Annotate explicitly (inference unboxes i32/i64/f64/bool) |

Codes are stable API: tools may match on them. Messages and hints are
human-facing text and may improve between releases.

`HC2101` is the honest catch-all: the analysis declined for a reason the
classifier does not model (for example, lets inside `loop { }` bodies,
which the block analysis does not visit).

## JSON schema

```json
{
  "version": 1,
  "file": "matmul.hml",
  "summary": {"sites": 8, "unboxed": 2, "boxed": 6, "ratio": 0.2500},
  "sites": [
    {"name": "i", "line": 5, "column": 0, "kind": "loop_counter",
     "decision": "unboxed", "native_type": "i32"},
    {"name": "sum", "line": 4, "column": 5, "kind": "typed_var",
     "decision": "boxed", "reason": "HC2105",
     "message": "escapes its scope (captured, stored, returned, or indexed)",
     "hint": "keep hot arithmetic in a local that is not captured, stored, or returned"}
  ]
}
```

- `kind` is one of `typed_var`, `inferred_var`, `loop_counter`,
  `accumulator`, `parameter`, `top_level`.
- `decision` is `"unboxed"` (with `native_type`) or `"boxed"` (with
  `reason`, `message`, and optionally `hint`).
- Lines and columns are 1-based; column 0 means unknown.
- `hemlock check --coverage --json` nests one such object per checked file
  under a top-level `"coverage"` array, alongside the usual `diagnostics`.

`hemlockc --coverage --json` prints the object alone, ready for a report
UI or CI dashboard to consume.

## Limits

- The report covers the entry file (like `hemlock check`), not imported
  modules.
- Reason classification replays the analyzer's checks; the decision itself
  is read from the analysis, so a surprising reason never means a wrong
  decision.
- Object-literal method bodies are not yet walked for sites.

## See Also

- [Compiler Optimizations](compiler-optimizations.md) — what unboxing does
  and the other optimization passes
- [Static Checking](static-checking.md) — `hemlock check` and its JSON
  diagnostics
