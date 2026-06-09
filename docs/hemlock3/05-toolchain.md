# Hemlock 3 · Toolchain

## 1. One command: `hml`

2.x ships six entry points (`hemlock`, `hemlockc`, `--lsp`, `--fmt`,
`--bundle`, external `hpm`). Production languages ship one. The `hml`
driver subsumes all of them; the underlying binaries remain (and remain
scriptable), but every doc, tutorial, and error message speaks `hml`:

| Command | Today | Notes |
|---|---|---|
| `hml run app.hml` | `hemlock app.hml` | interpreter; debug allocator guards on |
| `hml build` | `hemlockc …` | compiles per the manifest; `--release`, `--static` (Linux default, as in 2.6), `--target` |
| `hml test` | `make test` / ad-hoc | discovers `tests/**/*.hml`, runs `@stdlib/testing` suites, side-effect imports remove boilerplate |
| `hml fmt` | `hemlock --fmt` | unchanged engine; `--check` for CI |
| `hml lint` | — (new) | `.length`-in-byte-context, unsupervised `spawn`, gated-op-outside-`unsafe` (2024 edition), deprecated APIs |
| `hml lsp` | `hemlock --lsp` | unchanged engine |
| `hml bundle` | `hemlock --bundle` | `.hmlc`/`.hmlb`/`.hmlp`, unchanged |
| `hml doc` | — (new) | renders stdlib-style docs from `///` comments |
| `hml add <pkg>` / `hml install` / `hml vendor` | external `hpm` | absorbed; see §3 |
| `hml fix --edition 2026` | — (new) | mechanical migration; see [06-migration.md](06-migration.md) |

Distribution: `curl -fsSL hmlk.dev/get | sh` installs `hml` plus
toolchain; release artifacts carry signed checksums.

## 2. Project manifest: `hemlock.toml`

The "optional project manifests" roadmap item from `docs/versioning.md`,
made real:

```toml
[package]
name = "witchgrid"
version = "1.0.0"
edition = "2026"          # language edition — see 06-migration.md
hemlock = ">=3.0 <4"      # toolchain constraint

[dependencies]
jinja = "2"               # resolved via the hmlk.dev registry index
gn = { git = "https://github.com/Yotis-Studios/gn.hml", tag = "v0.2.0" }

[build]
bin = "src/main.hml"
static = true
embed = ["assets/"]       # paths the embed builtins may reach (sandboxing)

[profile.release]
optimize = true
```

- `hml run`/`build`/`test` look upward for `hemlock.toml`; **single-file
  scripts keep working with zero manifest** — `hml run script.hml`
  assumes the current edition and no deps. The manifest is for projects,
  not a tax on scripts.
- `hemlock.lock` records exact versions + SHA-256 of every dependency
  (registry tarball or git commit). Committed to VCS; `hml install
  --locked` is the CI mode and fails on drift. This, plus `hml vendor`
  (copies deps into `vendor/` for airgapped builds), is the
  supply-chain story.

## 3. Packages and the registry

hpm's GitHub-repos-as-packages model is kept — it is cheap, auditable,
and already works — and formalized:

- The **registry is an index, not a host**: a signed JSON index served
  from `hmlk.dev` mapping `name@version` → git URL + tag + tarball
  SHA-256. Packages live in their authors' repos.
- `import { x } from "@pkg/name";` resolves through the lockfile.
  `@stdlib/` stays reserved for tiers 1–2.
- Publishing: `hml publish` pushes a git tag and submits the index entry;
  names are first-come with the org reserving `@stdlib`/`@pkg` prefixes.
- Tier 3 spinouts (`jinja`, `matrix`, `vector` —
  [04-stdlib.md](04-stdlib.md)) are the registry's seed content, so the
  registry launches non-empty and dogfooded.

## 4. Targets

- **Linux x86_64 / macOS ARM64**: existing first-class targets, static
  linking default on Linux (2.6 behavior, specified).
- **WASM**: promote `docs/plans/hemlockscript-wasm.md` from plan to 3.x
  roadmap — interpreter-in-browser powers `hmlk.dev/play`; `hml build
  --target wasm32` for compiled modules is 3.1 scope, not a 3.0 blocker.
- **Windows**: explicitly *not* promised for 3.0 (WSL2 documented path).
  Saying so on the website beats an eternally-slipping checkbox.

## 5. CI surface (what "production-grade" means mechanically)

Specified, public, and badged on the README:

1. `make test` (interpreter), `make test-compiler`, `make parity` —
   parity remains the merge gate; new 3.0 features land with parity
   tests citing spec sections.
2. ASAN + LSAN lanes (existing) plus the leak-hunt stress suite running
   nightly against `scope`/`shared`/`select` — the new concurrency
   features get the same adversarial treatment that hardened 2.5.x.
3. `hml fmt --check`, `hml lint`, `tests/check_docs.py` (extended with
   the bytes-or-runes callout check) on every PR.
4. Release pipeline: signed checksums, static Linux + macOS binaries,
   `hmlk.dev/get` updated atomically with the GitHub release.
