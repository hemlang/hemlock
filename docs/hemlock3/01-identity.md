# Hemlock 3 · Identity & Rebrand

## 1. The problem

Hemlock 2.x markets itself as *"a small, unsafe language for writing
unsafe things safely."* That tagline is honest and it filtered for exactly
the early adopters the language needed. It is also the single biggest
obstacle to the next thousand users: nobody wants to write "we chose the
poison-themed language whose tagline contains 'unsafe' twice" in a
production design review. Witchgrid adopted Hemlock *despite* the
branding, because the engineering reality — single ~7MB static binary,
sub-second compiles, a stdlib that covers ops work — is genuinely strong.

The brand should catch up to the engineering.

## 2. Decision: keep the name, change the tree

**The language remains Hemlock.** Renaming would forfeit the `hemlang`
GitHub org, the `.hml` extension, hpm, both owned domains, all existing
search results, and the goodwill of every current user — to escape a
connotation we can simply reframe.

The reframe: **hemlock is a tree.** The Eastern Hemlock (*Tsuga
canadensis*) is a slow-growing, long-lived conifer — an evergreen
foundation species whose timber framed houses and railroads. The poison
plant (*Conium maculatum*, the thing that killed Socrates) is an unrelated
herb that happens to share the name. Hemlock 3 is named for the tree.

This gives the brand exactly the qualities a production language wants to
signal — **durable, structural, evergreen, unglamorous, load-bearing** —
without changing a single identifier anywhere in the ecosystem.

### Messaging

| | Hemlock 2.x | Hemlock 3 |
|---|---|---|
| Tagline | "A small, unsafe language for writing unsafe things safely." | **"Small language. Single binary. No surprises."** |
| Identity | The poison; danger as a feature | The tree; structure as a feature |
| Safety story | "Unsafe is a feature" | **"Unsafe is a scope"** — every capability kept, all of it greppable (see [02-language.md](02-language.md)) |
| Pitch | The power of C with scripting ergonomics | The same, *plus*: interpret while developing, compile to one static binary to ship |

Words to retire from headline marketing: *unsafe, poison, dangerous,
crash, footgun*. They remain accurate in reference docs (an `unsafe`
block is called an `unsafe` block), but they stop being the identity.

Words to promote: *explicit, small, single-binary, structural, boring on
purpose, no hidden behavior*.

### Logo / visual direction

Evergreen conifer mark (silhouette or single sprig of flat needles),
deep green + timber brown palette. Monospace wordmark, lowercase
`hemlock`. The mascot-free, infrastructure-tool aesthetic of Go/Zig
rather than the playful aesthetic of scripting languages.

## 3. Domains

Both owned domains get distinct jobs:

### `hemlang.dev` — the home
- Landing page, the 3.0 spec rendered from this directory, language
  guide, stdlib docs, blog/release notes.
- Already listed as the org website; canonical URL in all READMEs.

### `hmlk.dev` — the tool domain
Short, typeable, meant for terminals and code:
- **Installer:** `curl -fsSL hmlk.dev/get | sh`
- **Playground:** `hmlk.dev/play` (WASM interpreter build — see the
  existing `docs/plans/hemlockscript-wasm.md` plan, which this promotes
  from exploration to roadmap).
- **Package registry:** `hmlk.dev/p/<name>` package pages;
  the registry index that `hml add` resolves against
  (see [05-toolchain.md](05-toolchain.md)).
- **Module path shorthand:** package imports may use
  `import { x } from "@pkg/name";` resolved via the registry index served
  from this domain.

## 4. Naming inside the toolchain

The `hemlock` (interpreter) and `hemlockc` (compiler) binaries continue
to exist, but the user-facing entry point becomes one command:

- **`hml`** — the unified CLI (`hml run`, `hml build`, `hml test`,
  `hml fmt`, `hml add` …). Matches the file extension, three keystrokes,
  echoes the short domain. Specified in [05-toolchain.md](05-toolchain.md).
- File extension: `.hml` (unchanged). Bundle formats `.hmlc`/`.hmlb`/`.hmlp`
  (unchanged).
- GitHub org: `hemlang` (unchanged).

## 5. Trust artifacts

Branding for production trust is mostly *not* visual — it is published
commitments. Hemlock 3 ships with:

1. **A written specification** — this directory, maintained as normative,
   rendered at `hemlang.dev/spec`. Parity tests cite spec sections.
2. **Stability tiers for the stdlib** — explicit semver guarantees per
   module tier ([04-stdlib.md](04-stdlib.md)).
3. **Editions, not flag days** — code keeps compiling across major
   versions ([06-migration.md](06-migration.md)).
4. **An LTS policy** — 3.0 receives security and crash fixes for 18
   months after 3.1 ships.
5. **A security page** — `hemlang.dev/security`: disclosure contact,
   the crash-handler/ASAN/leak-hunt CI lanes documented, signed release
   checksums.
6. **Production users page** — Witchgrid and gn.hml as named case
   studies (with permission), because "who runs this in prod" is the
   first question every evaluator asks.
