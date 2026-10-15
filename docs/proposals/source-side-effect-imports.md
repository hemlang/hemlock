# Source Side-Effect Imports

> **Status: implemented in 2.7.0** as Option B (with the `@` package-prefix
> extension): `import "./foo.hml";` and `import "@stdlib/foo";` are
> side-effect source imports; bare imports of anything else keep the FFI
> shared-library semantics. See `docs/language-guide/modules.md`.

## Need

A test runner should be able to discover suites by importing test files for their module-load side effects. In that model, each suite file imports `@stdlib/testing` and calls `describe()`/`test()` at top level; the runner imports the suite files, then calls `run()`.

## Current workaround

Use an empty named import with an explicit source extension:

```hemlock
// all_tests.hml
import { run } from "@stdlib/testing";

import {} from "./intent_test.hml";
import {} from "./parser_test.hml";

let results = run();
```

This works because `import {} from "./intent_test.hml";` goes through normal source-module resolution and execution, while binding zero exported names into the runner.

Do not use this form for source tests today:

```hemlock
import "./intent_test";
```

Bare string imports are the existing FFI-load syntax. With a source-like path, they can fail in the dynamic loader with an error such as `Failed to load library './intent_test': cannot open shared object file`.

## Syntax options

### Option A: `import "./foo";`

Treat bare string imports as side-effect source imports when they look like source paths.

- Pros: familiar side-effect import spelling.
- Cons: collides with the existing FFI-load syntax, which is the source of the current failure mode.
- Possible disambiguation: `.hml` means source; no extension means FFI. That would be a meaningful behavior change for existing imports.

### Option B: `import "./foo.hml";`

Use the explicit `.hml` extension to select source-module loading; keep extensionless bare imports as FFI loads.

- Pros: backwards-compatible for extensionless FFI imports.
- Pros: concise for side-effect test-suite imports.
- Cons: changes behavior for any existing FFI import that names a `.hml`-suffixed shared library path, if such code exists.

### Option C: `use "./foo";`

Add a new keyword for side-effect source imports.

- Pros: no collision with FFI imports.
- Pros: clear semantic distinction between loading source modules and loading native libraries.
- Cons: requires lexer/parser/docs/tooling updates and reserves a new keyword.

### Option D: `import {} from "./foo";`

Keep using empty destructuring as the side-effect import spelling.

- Pros: works today for source modules without parser changes.
- Pros: no collision with FFI imports.
- Cons: visually odd because it looks like a named import but intentionally imports no names.
- Cons: test-runner authors need documentation so they do not reach for bare string imports and hit the FFI loader.

## Compatibility note

The least disruptive documented path today is Option D with an explicit `.hml` extension: `import {} from "./foo.hml";`. If a dedicated syntax is added later, existing test runners can migrate mechanically from empty imports to the chosen spelling.
