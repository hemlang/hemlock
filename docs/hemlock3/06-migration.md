# Hemlock 3 · Migration, Editions, and Phasing

## 1. Editions, not flag days

Major versions that break the world kill small languages. Hemlock 3
adopts editions (the Rust mechanism, scaled down):

- `hemlock.toml` declares `edition = "2024"` (2.x semantics) or
  `edition = "2026"` (this spec). Single files without a manifest:
  `hml run` assumes 2026 and downgrades 2026-only *errors* to warnings
  when the file would have been valid 2024 code — so existing scripts
  keep running while telling you what to fix.
- **One frontend, both editions.** The parser/resolver carries an edition
  flag; both backends honor it. Edition differences are deliberately
  few and lexical (list below) so this stays cheap.
- 2024-edition support is maintained through all of 3.x, removed no
  earlier than 4.0.

### Edition-gated differences (complete list)

| | 2024 | 2026 |
|---|---|---|
| Raw-pointer ops outside `unsafe` | allowed (warning) | error |
| `__string_from_bytes` | works (deprecation warning) | removed (use `string.from_bytes`) |
| `@stdlib/jinja`·`matrix`·`vector` | re-export shims with warning | gone; `@pkg/…` |
| `fn` in object literals, `select`, `scope`, `shared`, `embed*`, side-effect imports, generics | available in **both** editions — pure additions are not gated |

Everything additive lands in both editions; only removals/restrictions
gate. This keeps the matrix testable.

## 2. Breaking changes in 3.0 (total list)

1. Raw-pointer operations require `unsafe` blocks (2026 edition).
2. `__string_from_bytes` removed (2026) — mechanical rename.
3. Tier 3 stdlib modules move to packages (shimmed through 3.x in 2024
   edition).
4. New reserved words: `unsafe`, `scope`, `shared`, `select`, `embed`,
   `embed_bytes`, `embed_dir`. Collisions with user identifiers are the
   realistic migration pain; `hml fix` renames shadowed identifiers.
5. Default HTTP timeout 5s → 30s (behavioral; flagged in release notes;
   explicit `timeout_ms` everywhere is the documented best practice
   either way).

Nothing else in 2.x programs changes meaning. Notably **not** breaking:
`/` float semantics, `.length` (runes), mutable strings, semicolons,
deep-copy `spawn`, all 30 value types, the `.hml`/`.hmlc`/`.hmlb`/`.hmlp`
formats.

## 3. `hml fix` — mechanical migration

`hml fix --edition 2026` performs, with a diff-preview mode:

- Wrap statements using gated pointer ops in `unsafe { … }` (narrowest
  enclosing statement; emits a `// TODO(unsafe-audit)` marker).
- `__string_from_bytes(x)` → `string.from_bytes(x)`.
- `@stdlib/jinja` → `@pkg/jinja` imports (and `hml add jinja`).
- Rename identifiers that collide with new keywords.
- Flag (not auto-fix): `.length` used in byte contexts, unsupervised
  `spawn`.

Acceptance test: **gn.hml and Witchgrid must migrate with `hml fix` plus
under one hour of hand-edits each.** Both are open source; the migration
PRs are written by us, serve as the migration tutorial, and (with the
authors' blessing) become the launch case studies.

## 4. Implementation phasing

Evolve the existing codebase; every phase ships green parity.

### Phase A — 3.0-alpha: the unblockers (smallest code, biggest consumer pain)
- `http` v2 (`fetch` with headers/method/timeout) — kills Witchgrid's
  auth-in-body hack.
- `string.from_bytes`, `rfind`, one-arg `substr`, `f32/f64.from_bits/to_bits`.
- Side-effect imports; `fn` in object literals.
- `embed` / `embed_bytes` / `embed_dir`.
- Structured `error()` + exception backtraces; exception-path temporary
  cleanup (Memory Plan Gap 1).
- Debug allocator guards.

### Phase B — 3.0-beta: concurrency
- `scope` blocks; handle/close discipline at scope exit.
- `shared()` + `with/get/set`; `TYPEID_SHARED`.
- Channel `select` (+ `timeout`/`default` arms).
- Nightly ASAN/LSAN stress lanes for all three.

### Phase C — 3.0: surface and trust
- `unsafe` enforcement + editions in the frontend; `hml fix`.
- Unified `hml` CLI; `hemlock.toml` + lockfile; hpm absorbed; registry
  index on hmlk.dev.
- Stdlib tiering + Tier 3 spinout; minimal generics.
- Spec freeze: this directory → `hemlang.dev/spec`; parity tests
  annotated with spec sections.
- Rebrand rollout: site, tagline, README, security page, case studies.

### Post-3.0 (3.x roadmap, explicitly not blockers)
- WASM compile target (`--target wasm32`); playground ships earlier on
  the interpreter build.
- `hml doc` generator polish; Tree-sitter grammar; Emacs mode.
- Bytecode/IR exploration for the interpreter — *investigate only*: the
  tree-walker's simplicity is load-bearing for parity, and no consumer
  has named interpreter throughput as a blocker.

## 5. Versioning & support promises (published on hemlang.dev)

- Hemlock 3.0 ships with the spec; point releases never change spec'd
  behavior except to fix divergence from the spec.
- 2.6 receives crash/security fixes for 12 months after 3.0.
- 3.0 LTS: security and crash fixes for 18 months after 3.1.
- Parity between interpreter and compiler is a *specification guarantee*,
  not an implementation detail: any divergence is a release-blocking bug.
