# Static Lint (Diagnostics)

The Hemlock compiler (`hemlockc`) ships a **static lint pass** — a small
analysis that flags code which is well-typed and memory-safe but almost
certainly a mistake. Where the [type checker](../reference/type-system.md)
proves values fit their types and the [borrow checker](borrow-checker.md) tracks
resource ownership, the linter catches the everyday slip-ups you want to find
*before you ship a binary*: unreachable code, dead branches, self-assignment,
modulo-by-zero, and unused variables.

Like the borrow checker, it is **advisory by design.** It emits **warnings**,
not errors, and never changes what your program does. You can make it strict,
make it fatal, or turn it off.

```
Source (.hml)
    ↓
  Parse → AST
    ↓
  Resolve
    ↓
  Lint           ← this pass (warnings by default, sees your code as written)
    ↓
  Optimize → Type Check → Borrow Check
    ↓
  C Code Generation
```

The lint pass deliberately runs **before** the optimizer, so it diagnoses the
source you actually wrote rather than what is left after dead-code elimination.
Only your program's own top-level module is analysed; imported modules are
linted when *they* are compiled, so a clean build never buries you in warnings
about library code you did not write.

---

## What it catches

| Diagnostic | When | Default |
|------------|------|---------|
| **unreachable code** | a statement follows one that always diverges (`return`/`break`/`continue`/`throw`/`panic()`, an `if`/`else` whose arms all diverge, or an infinite `loop`/`while (true)` with no `break` bound to it) | on |
| **dead branch** | an `if`/`while` condition is a constant (`if (false)`, `while (0)`) | on |
| **redundant branch** | an `if (true) { … } else { … }` whose `else` can never run | on |
| **self-assignment** | `x = x`, `obj.f = obj.f`, or `a[i] = a[i]` — a no-op | on |
| **self-comparison** | `x < x` / `x > x` (always false), `x && x` / `x \|\| x` (redundant) | on |
| **modulo by zero** | `x % 0` with a literal zero divisor — traps at runtime | on |
| **duplicate field** | `{ a: 1, a: 2 }` — the later value silently wins | on |
| **duplicate case** | a repeated literal `switch` case — the second is unreachable | on |
| **unused variable** | a `let`/`const` that is never read in its scope | strict only |

Every diagnostic is something the compiler is **certain** about. The analysis is
intentionally conservative — for example, self-assignment is only reported when
both sides are side-effect-free, so `a[next()] = a[next()]` is left alone, and
`while (true)` (a deliberate idiom) is never flagged. Self-comparison exempts
`==`, `!=`, `<=`, and `>=` entirely: for floats every comparison with NaN is
false, so `x == x` is *not* always true and `x != x` is the canonical NaN
check — only `<` and `>` (false even for NaN) are reported. There are no false
positives to silence.

### Examples

```hemlock
fn classify(n: i32): string {
    if (n > 0) {
        return "positive";
        print("never runs");   // warning: unreachable code: control always
    }                          //          leaves via the return on line 3
    return "other";
}

fn dead(): i32 {
    if (false) {               // warning: condition is always false; this
        return 1;              //          branch never executes
    }
    let x = 10;
    x = x;                     // warning: self-assignment: 'x = x' has no effect
    return x % 0;              // warning: modulo by zero: traps at runtime
}
```

With `--lint-strict`, unused locals are flagged too. Names beginning with `_`
and function bindings are exempt by convention:

```hemlock
fn f(): i32 {
    let used = 1;
    let unused = 2;            // warning: variable 'unused' is declared but never used
    let _scratch = 3;          // exempt: leading underscore
    return used;
}
```

---

## Flags

| Flag | Effect |
|------|--------|
| *(default)* | Lint on; advisory warnings, build still succeeds |
| `--lint-strict` | Also flag unused variables |
| `--lint-error` | Treat every lint finding as an error and **fail the build** |
| `--no-lint` | Disable the lint pass entirely |

These compose with `--check`, which runs the static analyses (type, borrow,
lint) and stops before code generation:

```sh
hemlockc --check --lint-strict app.hml   # report everything, compile nothing
hemlockc --lint-error app.hml -o app      # refuse to build if anything is flagged
```

Use `--lint-error` in CI to keep unreachable code, dead branches, and
self-assignments out of a release binary, while leaving the default advisory
behaviour for everyday local builds.

---

## Why warnings, not errors

This mirrors Hemlock's stance everywhere else: *explicit over implicit, and the
programmer keeps control.* A dead branch or a `% 0` might be a placeholder you
are actively working on; the compiler points it out but does not stop you. When
you are ready to enforce a clean bill of health, opt in with `--lint-error`.

See also: [Borrow Checker](borrow-checker.md) ·
[Compiler Optimizations](compiler-optimizations.md)
