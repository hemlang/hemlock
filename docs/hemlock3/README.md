# Hemlock 3 — Specification & Direction

> **Named for the tree, not the poison.**

This directory is the design specification for Hemlock 3.0: the language,
the brand, the toolchain, and the migration path from 2.x. It is the
normative source for what 3.0 means; implementation work should cite
sections of these documents.

| Document | Contents |
|----------|----------|
| [01-identity.md](01-identity.md) | The rebrand: positioning, naming, domains, messaging |
| [02-language.md](02-language.md) | Scoped `unsafe`, strings & bytes, errors, generics, ergonomics |
| [03-concurrency.md](03-concurrency.md) | Structured scopes, `shared` values, channel `select` |
| [04-stdlib.md](04-stdlib.md) | Stdlib tiering, `http` v2, `embed`, promoted builtins |
| [05-toolchain.md](05-toolchain.md) | The unified `hml` CLI, manifests, packaging, registry |
| [06-migration.md](06-migration.md) | Editions, breaking changes, phasing, migration tooling |

---

## Executive summary

Hemlock 2.6 is a working, shipping language: ~87K LOC across a
tree-walking interpreter and a C-emitting compiler held to byte-identical
output by 259 parity tests, 53 stdlib modules, an LSP, a formatter, and a
bundler. It has real production consumers — **Witchgrid** runs a GPU
inference fleet on compiled Hemlock binaries daily, and **gn.hml** ships a
binary WebSocket game-networking framework on it.

Those two projects, more than anything internal, define what 3.0 must be.
Their documented experience converges on five findings:

1. **The concurrency model is the deepest constraint.** `spawn()`'s
   deep-copy-everything semantics make shared-state designs (event
   emitters, server objects mutated by callbacks) impossible without
   architectural contortions, and the lack of any tie between a resource's
   lifetime and the tasks using it produced real use-after-free bugs.
   → [03-concurrency.md](03-concurrency.md): structured `scope` blocks,
   explicit `shared()` values, channel `select`.

2. **"Unsafe as a brand" blocks adoption; "unsafe as a scope" enables it.**
   Production users chose Hemlock *despite* the tagline, for the
   single-binary deploys and sub-second compiles. 3.0 keeps every
   capability but moves raw-pointer operations behind lexical `unsafe { }`
   blocks, making the entire unsafe surface of a program greppable.
   → [02-language.md](02-language.md).

3. **The bytes ↔ strings ↔ numbers story has production scar tissue.**
   UTF-8 length confusion truncated HTTP responses; bytes-to-string
   required an undocumented `__string_from_bytes` dunder; binary parsers
   skipped float fields for want of bit-casts.
   → [02-language.md](02-language.md) §3, [04-stdlib.md](04-stdlib.md).

4. **The toolchain is good parts without a whole.** Interpreter, compiler,
   LSP, formatter, bundler, and an external package manager (hpm) are six
   entry points where production languages have one. 3.0 ships a single
   `hml` CLI, a project manifest, a lockfile, and first-party asset
   embedding. → [05-toolchain.md](05-toolchain.md).

5. **Trust is a deliverable.** A written spec, a tiered stdlib with
   explicit stability guarantees, editions instead of flag days, and an
   LTS policy are what let someone defend choosing Hemlock in a design
   review. → [01-identity.md](01-identity.md), [06-migration.md](06-migration.md).

## The headline decision: evolve, don't rewrite

Hemlock 3 is **a major revision of this codebase, not a new
implementation**. The shared frontend + dual backend architecture with
parity enforcement is an asset few small languages have; the 1,400-file
test suite and two production consumers are exactly what a rewrite would
forfeit. 3.0 lands as an **edition** (see [06-migration.md](06-migration.md)):
2.x programs keep running under `edition = "2024"` semantics while new
code opts into `edition = "2026"`.

The name stays **Hemlock**, re-grounded in the hemlock *tree* — and the
fileext (`.hml`), the org (`hemlang`), and both owned domains
(`hemlang.dev`, `hmlk.dev`) stay exactly as they are. Details in
[01-identity.md](01-identity.md).

## What does NOT change

The philosophy survives intact — 3.0 sharpens it rather than softening it:

- Explicit over implicit: mandatory semicolons, no ASI, no GC, no hidden
  refcount magic. (`unsafe { }` blocks *add* explicitness.)
- Manual memory: `alloc`/`free`, `buffer` for checked access, `ptr` for raw.
- Dynamic by default, typed by choice; runtime type tags.
- C-like syntax; `{}` always required.
- Parity-first development: interpreter and compiler produce identical
  output, enforced in CI.
- The interpret-for-dev / compile-for-deploy loop, and single-binary output.

The litmus test stands: *"Does this give the programmer more explicit
control, or does it hide something?"* Every 3.0 feature in these documents
was checked against it.
