# Name Resolution

Hemlock resolves every identifier in a program **before** any of it runs.
The pass is in the shared frontend (`src/frontend/resolver/name_resolve.c`),
so the interpreter, `hemlockc` and `hemlock check` all answer "does this name
exist?" the same way.

```
$ cat repro.hml
import { time_ms } from "@stdlib/time";
spawn(async fn() { sleep(0.2); print("fired"); });
print("done");

$ hemlock repro.hml
repro.hml:2:20: error: undefined name 'sleep' (did you mean to import it from @stdlib/time?)
1 error generated

$ hemlockc repro.hml
repro.hml:2:20: error: undefined name 'sleep' (did you mean to import it from @stdlib/time?)
1 error generated

$ hemlock check repro.hml
repro.hml:2:20: error: undefined name 'sleep' (did you mean to import it from @stdlib/time?) [names]
1 error, 0 warnings (1 file checked)
```

## Why it is an error

Hemlock has no implicit declaration. A name nothing defines throws
`Undefined variable` the moment control reaches it, and assigning to an
undeclared name throws the same way. "The moment control reaches it" is the
problem: inside a branch that rarely runs, or a spawned task nobody joins,
that moment may be weeks after deploy, or never.

For `hemlockc` it is also a hard requirement rather than a nicety. An
identifier that reaches code generation unresolved becomes a C symbol nobody
defined, and the build fails inside generated code:

```
/tmp/hemlock_eXxbTX.c:62:22: warning: implicit declaration of function '_mod0_fn_sleep'
/tmp/hemlock_eXxbTX.c:62:22: error: invalid initializer
```

Nothing in that points back at the Hemlock source. The compiler now refuses
to emit C for a name it cannot resolve.

## What it covers

The pass walks the entry file **and every module it imports, transitively**,
so a diagnostic names the file the identifier is actually in:

```
$ hemlock check server.hml
lib/handlers.hml:7:5: error: undefined name 'sleep' (did you mean to import it from @stdlib/time?) [names]
```

It understands every way Hemlock introduces a name:

| Form | Example |
|------|---------|
| Builtins and global constants | `print`, `spawn`, `i32`, `TYPEID_I32`, `args` |
| Module-level declarations | `let`, `const`, `fn`, `define`, `enum`, `type`, `extern fn` |
| Named imports | `import { a } from "m"` |
| Renamed imports | `import { a as b } from "m"` |
| Namespace imports | `import * as ns from "m"` → `ns` |
| Star imports | `import * from "m"` → every export of `m` |
| Function parameters | positional, optional, `ref`, `const`, `...rest` |
| Closure captures | free variables of an anonymous function |
| `async fn` bodies | including one passed straight to `spawn` |
| Loop bindings | `for (let i = ...)`, `for (k, v in ...)` |
| Pattern bindings | `match` bindings, typed, OR, object and array destructuring |
| `catch` parameters | `catch (e)` |
| `self` | bound on every method call |

Declarations are **hoisted within their scope**, because forward references
are legal Hemlock: mutual recursion between top-level functions works, and a
function body may read a `let` declared below it. Seeding each scope with
every name its statements declare keeps both working.

## What it does not cover

- **Use before assignment.** `print(x); let x = 1;` resolves: `x` exists in
  the scope, it is just still null when read. That is a separate question
  this pass deliberately does not answer.
- **Whether an imported name is actually exported.** `import { nope } from
  "./m.hml"` binds `nope`; the module system reports the missing export.
- **Types.** Unknown type names are the type checker's business.
- **`gpu fn` bodies.** A kernel is a different language with its own builtins
  (`global_id`, `barrier`, `local_array`) and its own validation pass in the
  GPU frontend. Both the kernel AST and its lowered CPU-emulation body are
  machine-generated, so neither is checked here.

## When it stays quiet

The pass over-approximates the set of defined names on purpose: every name it
reports is one that *would* fail at runtime. Where it cannot see all the
bindings a scope might hold, it reports nothing for that scope rather than
guessing. That happens when a module cannot be read or parsed, or when
`import *` names a module that failed to load. The failure itself is still
reported — a module that will not parse produces its own diagnostics, named
by that module's file and line.

## Hints

The suggestion after the name is best-effort, in this order:

1. a module the file already imports exports that name — a missing import name;
2. a stdlib module exports it — a missing import;
3. something spelled almost the same is in scope — a typo.

Where several modules export the name, all of them are collected and **sorted**,
so the message does not depend on the order they happened to be discovered in:

| Matches | Message |
|---------|---------|
| 1 | `did you mean to import it from @stdlib/time?` |
| 2–3 | `did you mean to import it from @stdlib/datetime or @stdlib/time?` |
| more | `exported by 9 modules, among them @stdlib/args, @stdlib/csv and @stdlib/json` |

Past three the count leads, because the useful fact is that the name is
ambiguous and the choice is the reader's. `parse` is exported by nine stdlib
modules, and naming whichever one `readdir()` returned first gave a different
suggestion on a different machine — usually the wrong one.

## Escape hatch

`hemlockc` has none: emitting C that references undefined symbols is never
useful.

The interpreter's startup pass costs one extra parse of the program, and
`--no-name-check` skips it:

```bash
hemlock --no-name-check script.hml
```

Use it to shave the parse off process startup, or to run a program whose
unreachable code names something that does not exist. `hemlock check` always
runs the pass — it is the pre-deploy gate.

### Code that never runs

The pass does not care whether a line is reachable, so a program that has
been running for months with a latent undefined name in a dead function now
fails at startup instead. That is the point — the name was always wrong, and
waiting for control to reach it is how one stayed in a shipped game server
for weeks — but it does mean an existing tree can stop starting after an
upgrade.

The usual cause was an `import` inside a function body, which parsed and bound
nothing, so every name it was meant to provide was undefined. That is now a
parse error at the import itself:

```hemlock
fn read_command() {
    import { parse } from "@stdlib/json";   // error: import must be at
    return parse(read_line());              // module top level
}
```

Move the import to module top level. An FFI library import
(`import "libc.so.6"`) does work below top level and is unaffected.

For anything else, `--no-name-check` gets the program starting again in the
meantime, but the name is still undefined: the program will fail the moment it
reaches that code.

## Builtin names

`include/builtin_globals.h` is the single source of truth for the names the
runtime installs in the global environment. The interpreter expands its lists
to register them; this pass expands the same lists to recognize them. One
list means a new builtin cannot be visible to one and undefined to the other,
and `tests/unit/test_builtin_globals_sync.sh` fails the build if the
hand-written code around those lists drifts.

`HML_COMPILER_ONLY_BUILTINS` in that header lists names `hemlockc` recognizes
that the interpreter does not register — `to_string`, `string_byte_length`,
`strerror` and friends. That is a parity gap, not a design decision: those
names work compiled and throw `Undefined variable` interpreted. The resolver
accepts them for both backends so it never rejects code one of them runs
today; when the gap closes, the list shrinks.

## Tests

| Suite | What it pins |
|-------|--------------|
| `make test-names` | `hemlockc` and the interpreter report an undefined name with identical text |
| `make test-check` | `hemlock check`'s own diagnostic format, including the imported-module case (`tests/check/names_*.hml`) |
| `tests/check/neg_names_scopes.hml` | Every binding form above, asserting a clean report — the false-positive guard |
| `make test-builtin-sync` | `builtin_globals.h` and the interpreter's registration agree |
| `tests/check/names_nested_import.hml` | a source-module import below top level is a parse error |
| `tests/check/neg_nested_ffi_import.hml` | an FFI import below top level is still legal |
| `tests/check/names_hint_two_modules.hml` | a name exported by two imported modules names both, sorted |

## See Also

- [Static Checking](../advanced/static-checking.md) — `hemlock check` as a whole
- [Modules](../language-guide/modules.md) — imports, exports, re-exports
- [Static Lint](../advanced/lint.md) — the lint pass
