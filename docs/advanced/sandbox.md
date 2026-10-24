# Sandbox Mode

`--sandbox` runs a program with dangerous capabilities switched off. It is
available on both backends:

```bash
hemlock --sandbox script.hml          # interpreter
hemlock --sandbox ./data script.hml   # also confine file reads to ./data
hemlockc --sandbox script.hml -o app  # compiled; restrictions are baked in
```

## What is restricted

| Flag (`hemlock_limits.h`) | Blocks |
|---|---|
| `HML_SANDBOX_RESTRICT_FFI` | `import "lib.so"`, `extern fn` |
| `HML_SANDBOX_RESTRICT_NETWORK` | sockets, HTTP, WebSocket |
| `HML_SANDBOX_RESTRICT_PROCESS` | `exec`, `fork`, `posix_spawn`, pipes |
| `HML_SANDBOX_RESTRICT_FILE_WRITE` | every file write |
| `HML_SANDBOX_RESTRICT_FILE_READ` | reads outside `DIR` (only when `DIR` is given) |
| `HML_SANDBOX_RESTRICT_SIGNALS` | `signal`, `raise`, `kill`, `abort` (interpreter only) |
| `HML_SANDBOX_RESTRICT_UNSAFE_MEM` | raw pointers (see below) |

### Raw memory

Raw pointers are unchecked by design, so a script that can read or write
through one can reach anything in the process, including the interpreter's own
sandbox flags. Under `--sandbox` every operation that creates, derives, or
dereferences a raw pointer raises a catchable error:

- `alloc`, `talloc`, `realloc`, `buffer_ptr`, `"...".byte_ptr()`
- `ptr_offset`, `ptr + n` / `ptr - n`, `ptr[i]`
- `ptr_read_*`, `ptr_deref_*`, `ptr_write_*` and `ptr_to_buffer` on a `ptr`;
  `ptr_read_ptr`, `ptr_deref_ptr`, `ptr_write_ptr` on anything (they turn
  bytes into addresses or addresses into bytes)
- `memset`/`memcpy`/`free` given a `ptr`
- `@stdlib/atomic`, `@stdlib/mmap`, and `@stdlib/bytes` reads/writes on a `ptr`

`buffer` stays available, along with `ptr_read_*`/`ptr_write_*`,
`memset`/`memcpy`, and `free` on buffers, because those are bounds-checked.

```hemlock
try {
    alloc(8);
} catch (e) {
    print(e);  // ... Sandbox violation: raw memory access (alloc()) is not allowed in sandbox mode
}
let b = buffer(8);
ptr_write_i32(b, 42);  // fine: bounds-checked
```

## What it is not

The sandbox is a policy layer inside the process, not an isolation boundary.
It turns accidental or casual misuse into clear, catchable errors. It does not
defend against bugs in the runtime itself, and stdlib handles such as compiled
regexes are untyped `ptr` values, so passing one handle where another kind is
expected is not caught. To contain hostile code, run it under OS isolation too
(a container, `bwrap`, Landlock/seccomp, a separate user).

Known differences between backends: a violation of the other restrictions exits
a compiled program instead of throwing, and `HML_SANDBOX_RESTRICT_SIGNALS` is
interpreter-only. Raw-memory violations throw on both backends.
