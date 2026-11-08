# Embedding Hemlock

`libhemlock_embed.a` is the interpreter as a library: a host program (a game
engine, an editor, a server) compiles Hemlock scripts, runs them, calls their
functions, and lets them call back into the host. The API is one C header,
`include/hemlock_embed.h`, that also compiles as C++.

The Godot engine module in [nbeerbower/godot](https://github.com/nbeerbower/godot)
(`modules/hemlock`) is built on it, and is a worked example of everything
below.

## Building

```bash
make embed
```

That builds `build/libhemlock_embed.a`. Link it with the system libraries the
interpreter was configured with, which `make embed-ldflags` prints:

```bash
cc host.c -Ipath/to/hemlock/include path/to/hemlock/build/libhemlock_embed.a $(make -s -C path/to/hemlock embed-ldflags)
```

The archive is built from the same objects as the `hemlock` binary. On a
toolchain that does not build position-independent code by default, add
`EXTRA_CFLAGS=-fPIC` to link it into a shared object.

## The model

| Object | What it is |
|--------|------------|
| `HmlVM` | One interpreter: a global environment (the builtins plus whatever the host defines) and a module cache, so a stdlib module imported by many scripts is executed once. |
| `HmlScript` | A parsed, name-checked source file whose imports are loaded. Compiling never runs it. Refcounted; each instance holds a reference. |
| `HmlInstance` | One run of a script's top level in a fresh environment, a child of the VM's globals. Top-level `let`s are the instance's state, top-level `fn`s its methods. Two instances of a script share nothing but imported modules. |

```c
#include "hemlock_embed.h"

HmlVMConfig cfg;
hml_vm_config_init(&cfg);              /* name checking on */
cfg.stdlib_path = "/opt/hemlock/stdlib";
HmlVM *vm = hml_vm_new(&cfg);

HmlEmbedStatus st;
HmlScript *script = hml_script_compile(vm, source, "/abs/path/player.hml", &st);
if (!script) {
    fprintf(stderr, "%s:%d: %s\n", hml_vm_last_error_file(vm),
            hml_vm_last_error_line(vm), hml_vm_last_error(vm));
}

HmlInstance *player = hml_instance_new(script, NULL, NULL, 0, &st);

HmlEmbedValue dt = hml_embed_float(0.016), result;
if (hml_instance_call(player, "update", &dt, 1, &result) != HML_EMBED_OK) {
    fprintf(stderr, "%s\n", hml_vm_last_error(vm));
}
hml_embed_value_free(&result);

hml_instance_free(player);
hml_script_release(script);
hml_vm_free(vm);
```

`path` is the script's absolute path: relative imports resolve against its
directory and messages name it. The file need not exist there; the host
passes the source.

## Values

Values cross the boundary as `HmlEmbedValue`, a tagged tree that the host
owns. Nothing the host holds points into interpreter memory, so the host has
no refcounts to get wrong. The cost is a copy at every crossing.

| Kind | Hemlock side |
|------|--------------|
| `HML_EMBED_NULL`, `BOOL`, `STRING` | `null`, `bool`, `string` (UTF-8, length in bytes) |
| `HML_EMBED_INT` | every integer type and `rune`, as `i64`. Host to Hemlock gives `i32` when the value fits and `i64` otherwise, as an integer literal would |
| `HML_EMBED_FLOAT` | `f32` / `f64`. Host to Hemlock gives `f64` |
| `HML_EMBED_ARRAY` | array |
| `HML_EMBED_OBJECT` | object. `type_name` is the `define` name, or NULL for an anonymous object |
| `HML_EMBED_HANDLE` | an opaque `u64` naming a host object (see below) |
| `HML_EMBED_OPAQUE` | a Hemlock-only value (function, `ptr`, task, ...). It reads as null on the host side, and `.as.s` names its type for messages |

Build values with `hml_embed_int()`, `hml_embed_cstring()`,
`hml_embed_array()` + `hml_embed_array_push()`, `hml_embed_object()` +
`hml_embed_object_set()` (the setters take ownership of the item), copy them
with `hml_embed_value_copy()`, and free them with `hml_embed_value_free()`.
Nesting deeper than `HML_EMBED_MAX_VALUE_DEPTH` (or a cycle) stops at that
depth.

### Host objects: handles

A handle is a `u64` that the host defines, such as an object id. How a script
sees one is up to the host: `hml_vm_set_handle_wrapper(vm, "wrap")` names a
global Hemlock function that turns the `u64` into the value scripts get,
usually an object whose methods call back into the host. Going the other
way, any object with a `u64` field named `__handle` converts back to
`HML_EMBED_HANDLE`, so passing the wrapper object to a native hands the
native its handle. With no wrapper set, a handle arrives as
`{ __handle: <u64> }`.

## Calling into the host

```c
static int host_log(void *userdata, const HmlEmbedValue *args, int n,
                    HmlEmbedValue *result, char *error, size_t error_size) {
    if (n != 1 || args[0].kind != HML_EMBED_STRING) {
        snprintf(error, error_size, "log(message) takes one string");
        return 1;                     /* raises a Hemlock exception */
    }
    my_logger(userdata, args[0].as.s.data);
    *result = hml_embed_null();
    return 0;
}

hml_vm_define_native(vm, "log", host_log, logger);
```

A native is a global Hemlock function (`fn log(...args)`) that packs its
arguments and calls the host. A nonzero return raises the message as a
catchable exception at the script's call site. Define natives before you
compile the scripts that use them, so that name checking knows them.

`hml_vm_eval(vm, source, name)` runs host Hemlock code (a prelude) in the
global environment. Its top-level definitions become globals for every
script compiled afterwards, and name checking accepts them. Globals the host
binds some other way can be announced with `hml_vm_declare_global()`. The
Godot module, for example, declares `owner`, which it binds per instance
through `hml_instance_new()`'s predefined names.

## Inspecting a script without running it

An editor needs the outline and the inspector before any code runs, so these
calls read the AST:

- functions: `hml_script_function_count/name/line/arity/param_count/param_name/has_rest`
- variables: `hml_script_variable_count/name/line/exported/is_const`
- `hml_script_variable_type()`: the annotation as written (`"f64"`, `"string?"`,
  `"array<i32>"`, `"Vector2"`), or NULL
- `hml_script_variable_default()`: the initializer when it is made only of
  literals (numbers, strings, bools, null, runes, negation, array and object
  literals). A float annotation makes an integer literal a float, as it would
  at runtime. A computed initializer returns `HML_EMBED_NOT_FOUND`.

`export let` marks a variable as exported, which is how the Godot module
picks inspector properties.

## Errors

Every call that runs Hemlock code returns an `HmlEmbedStatus`:
`PARSE_ERROR`, `NAME_ERROR` (see [Name Resolution](../reference/name-resolution.md)),
`LOAD_ERROR` (an import failed), `RUNTIME_ERROR` (uncaught exception),
`NOT_FOUND`, or `BAD_ARGUMENT`. The message, file, line and column stay in
`hml_vm_last_error*()` until the next call. A runtime error points at the
innermost frame in user code. Frames in the embedding's own synthesized
sources, and calls of `__`-prefixed (internal) functions such as a prelude's
helpers, are skipped. After an error the VM is usable as before: the
exception is cleared and the call stack unwound.

An uncaught exception in a module's top level is reported the same way. It
does not `exit(1)` the host as it would in the `hemlock` CLI. `panic()` and
`exit()` still end the process, because that is their contract and embedding
does not change it.

## What it does not do

- **Threads.** A VM is single-threaded, so call it from one host thread at a
  time. Scripts can still `spawn` their own tasks. Some interpreter state
  (the current source file named in messages) is process-wide, so two VMs
  must not run at the same time either. A host that calls in from several
  threads needs one lock around the VM. The Godot module uses a recursive
  mutex, because Hemlock calls the engine and the engine can call back into
  Hemlock on the same thread.
- **Sandboxing.** A script can do anything the `hemlock` binary can: files,
  FFI, processes, raw memory.
- **Module reloading.** A VM executes each imported module once and caches
  it, which is how importers share a module's state. A changed module is not
  re-read, and a module whose top level threw stays failed for that VM.
  Recompiling a *script* is always fresh.
- **A module filesystem.** Imports are read from disk through the normal
  module resolution, so a host that packs scripts into an archive has to put
  imported modules where the resolver can read them.
