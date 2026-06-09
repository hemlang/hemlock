# Hemlock 3 · Standard Library

## 1. Tiering: promise less, deliver harder

53 modules under one stability promise is a liability: nobody believes
`jinja` and `alloc` deserve the same compatibility guarantee, so the
guarantee is worth little. 3.0 splits the stdlib into explicit tiers,
published on hemlang.dev and enforced in CI:

### Tier 1 — `core` (semver-locked to the language)

Frozen API within a major version; breaking changes only at editions;
parity-tested; every module a Witchgrid/gn.hml-class daemon needs:

> `fs`, `path`, `os`, `env`, `process`, `time`, `datetime`, `net`,
> `http`, `json`, `strings`, `bytes`, `encoding`, `math`, `args`,
> `signal`, `collections`, `testing`, `assert`, `logging`, `fmt`,
> `iter`, `random`, `hash`, `regex`, `atomic`, `arena`, `async`,
> `glob`, `url`

### Tier 2 — `extra` (ships with the toolchain, versioned looser)

Best-effort API stability; may evolve between minors with deprecation
warnings:

> `sqlite`, `websocket`, `unix_socket`, `crypto`, `compression`, `csv`,
> `toml`, `yaml`, `mmap`, `ipc`, `terminal`, `termios`, `shell`,
> `retry`, `semver`, `uuid`, `decimal`, `debug`, `ffi`, `async_fs`

### Tier 3 — spun out to packages (`@pkg/…` via the registry)

Domain libraries that don't belong in a language's trust surface:

> `jinja`, `matrix`, `vector` (USearch), and the proposed vector-db work

Imports stay `@stdlib/<name>` for tiers 1–2. Tier 3 moves to
`@pkg/<name>` with a 3.0 deprecation shim (`@stdlib/jinja` re-exports
`@pkg/jinja` with a warning for one minor cycle).

---

## 2. `http` v2 — the largest live production wart

Witchgrid smuggles bearer tokens *inside JSON bodies* on every
control-plane call because the 2.x POST path drops custom headers. The
3.0 client is one request primitive with everything on it:

```hemlock
import { fetch } from "@stdlib/http";

let res = fetch("https://api.example.com/v1/models", {
    method: "POST",                          // any method
    headers: { "Authorization": "Bearer " + tok,
               "Content-Type": "application/json" },
    body: json.stringify(payload),           // string | buffer
    timeout_ms: 30000,                       // no surprise 5s default:
                                             // default is 30s, always overridable
});
res.status;        // i32
res.headers;       // object (lowercased keys)
res.body;          // buffer  — bytes, not string: the UTF-8 decision is the caller's
res.text();        // string.from_bytes(res.body) convenience
```

- `http_get`/`http_post`/`http_request` remain as thin wrappers over
  `fetch` (edition-stable), now passing headers through on **all**
  methods.
- Header values containing CR/LF are rejected (injection hardening,
  landed 2.6 — specified here).
- Streaming/SSE keeps the existing API, gains `headers` and
  `timeout_ms` options.
- `res.body` being a `buffer` (with `.text()` for the common case) makes
  the byte/rune boundary explicit at the one place it caused production
  data corruption.

---

## 3. Promotions out of the shadows

| 2.x reality | 3.0 |
|---|---|
| `__string_from_bytes` (undocumented dunder, used by both known production consumers) | `string.from_bytes(src, encoding?)` — documented, validated ([02-language.md](02-language.md) §3.1) |
| bash+python `embed_assets.sh` codegen | `embed` / `embed_bytes` / `embed_dir` builtins ([02-language.md](02-language.md) §6) |
| Manual `register()` exports in test files | side-effect imports ([02-language.md](02-language.md) §5); `@stdlib/testing` auto-registers `describe` blocks on import |
| Per-project f32 bit-cast hacks via temp `alloc` | `f32.from_bits` / `to_bits` builtins |

## 4. FFI isolation (specified, was a bug class)

The 2.x compiler's single global `_ffi_lib` (last `import "lib.so"` wins)
silently broke `@stdlib/sqlite` when `@stdlib/uuid` loaded after it. The
fix is now **specified behavior**: each module's `extern fn` declarations
bind to the library imported *in that module's file*, resolved at module
load, per-module handle table. A parity + regression test pins it (the
Witchgrid incident as a test case).

## 5. `net` additions

- `TcpListener`/`TcpStream` keep the line/`read(n)` altitude Witchgrid
  praised — no framework creep.
- New: `stream.set_read_timeout(ms)`, accepted-socket refcounting
  (landed 2.5.x, specified), and `listener.accept_nonblocking()` →
  `null | stream` for `select`-driven servers.

## 6. Documentation contract

Every Tier 1 module's doc page gains a **"Bytes or runes?"** callout
where relevant, and every function that takes a length/offset documents
which unit it means. This is a docs-CI check (`tests/check_docs.py`
extended), because the ambiguity already corrupted production traffic
once.
