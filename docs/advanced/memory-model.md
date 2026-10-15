# The Hemlock Memory Model

> What may cross a thread boundary, what happens when it does, and what you
> can rely on.

Hemlock's concurrency is real: `spawn()` creates an OS thread (pthread), and
spawned tasks run in parallel on separate cores. That makes the question
"which memory operations in one task are visible to another, and in what
order?" a real question with real consequences. This document is the answer —
the *intended semantics*, stated precisely enough that a divergence between
the two backends (or between the docs and the code) is a bug in the
implementation, not a reinterpretation of the spec.

This document is normative. Where older documentation (or a backend)
disagrees with it, this document wins.

- [The model in one paragraph](#the-model-in-one-paragraph)
- [Value taxonomy: copied, transferred, shared](#value-taxonomy-copied-transferred-shared)
- [Happens-before: the synchronization edges](#happens-before-the-synchronization-edges)
- [The race-free fragment (and its theorem)](#the-race-free-fragment-and-its-theorem)
- [Shared bindings are NOT a communication channel](#shared-bindings-are-not-a-communication-channel)
- [The unsafe fragment](#the-unsafe-fragment)
- [Sharp edges and non-guarantees](#sharp-edges-and-non-guarantees)
- [Implementation notes (non-normative)](#implementation-notes-non-normative)

---

## The model in one paragraph

Hemlock is a **message-passing** language. Data crosses threads at exactly
four sanctioned points: **spawn arguments** (deep-copied in), **task
results** via `join`/`await` (ownership transferred out), **task
exceptions** re-thrown at `join`, and **channel `send`/`recv`** (reference
transferred, with a happens-before edge). Each sanctioned point is also a
synchronization point: everything the sending side did before the handoff
is visible to the receiving side after it. Anything *else* that two tasks
can both reach — a captured variable, a module global, a raw `ptr`, an
object that the sender kept mutating after `send()` — is shared mutable
state, and Hemlock does not synchronize it for you. In keeping with
"unsafe is a feature," sharing it is allowed; making it correct is your
job (channels, `join()` ordering, or `@stdlib/atomic`).

## Value taxonomy: copied, transferred, shared

What `spawn(fn, args...)` / `spawn_with(opts, fn, args...)` /
`detach(fn, args...)` do to each argument, and what a value means once it
has crossed via a channel or a task result:

| Type | At spawn (as argument) | Via channel / task result |
|---|---|---|
| `i8`…`u64`, `f32`, `f64`, `bool`, `rune`, `null` | copied by value | copied by value |
| `string` | **deep copy** | reference (aliased!) |
| `array` | **deep copy** (recursive) | reference (aliased!) |
| `object` | **deep copy** (recursive) | reference (aliased!) |
| `buffer` | **deep copy** | reference (aliased!) |
| `ptr` | **shared by reference** — no copy, no tracking | shared by reference |
| `channel` | shared (that's the point) | shared |
| `task` | shared (coordination handle) | shared |
| `function` | shared (code is immutable; its captured environment is shared — see below) | shared |
| `file` / `socket` | shared (kernel object; OS serializes syscalls, not your protocol) | shared |

Three consequences worth reading twice:

1. **Spawn isolates; channels do not.** `spawn` deep-copies mutable
   compound values precisely so the new thread cannot race with you.
   `ch.send(v)` does **not** copy `v` — it transfers a *reference*. After
   sending an array/object/string/buffer, the sender still holds an alias
   to the same heap object as the receiver. The sanctioned discipline is
   **ownership transfer**: after `send(v)`, the sender must stop reading
   and writing `v`'s interior (dropping the variable or reassigning it is
   fine — reference counts are atomic). A sender that keeps mutating a
   sent value is racing with the receiver. This is deliberate: copying at
   every send would be hidden O(n) work, and Hemlock does not hide work.

2. **`ptr` is shared, always.** Contrary to what older docs claimed, raw
   pointers *can* be passed to spawned tasks and through objects that
   contain them — by reference, uncopied, untracked. This is the intended
   escape hatch (FFI handles, shared native buffers). The programmer owns
   synchronization and lifetime: `join()` the consumer before `free()`.

3. **Deep copy follows the deep-copy table, recursively.** An object
   passed to spawn is deep-copied, but a `ptr`, `channel`, or `task`
   *inside* it is shared per the table. A `ref` parameter binding cannot
   cross `spawn` at all (it becomes `null`).

Task results (`join`/`await` return values) are transferred, not copied:
the joining thread receives a reference to the very objects the task
built. That is safe — not because of a copy, but because of a
happens-before edge: the task has terminated before `join` returns, so no
concurrent accessor exists (unless the task deliberately leaked the value
elsewhere, e.g. also sent it on a channel — then the aliasing rules above
apply).

## Happens-before: the synchronization edges

Hemlock guarantees exactly these cross-thread ordering edges. "A
happens-before B" means every memory write performed before A is visible
to code running after B, on any core.

1. **Program order** within one task.
2. **Spawn**: everything the spawner did before `spawn(...)` returns
   happens-before the first statement of the task body. (Reading
   pre-initialized globals from a task is therefore safe — if nobody
   writes them afterwards.)
3. **Join/await**: the last statement of a task body happens-before
   `join(t)` / `await t` returning in the joiner. The same edge carries
   the re-thrown exception.
4. **Channel send→recv**: `ch.send(v)` (and a successful
   `send_timeout`) happens-before the `recv()` / `recv_timeout()` /
   `select()` that returns `v`. The receiver always observes the fully
   constructed value and everything the sender wrote before sending it.
5. **Channel close→drain**: `ch.close()` happens-before a `recv()` that
   returns `null` because the channel is closed and empty.
6. **Atomics**: operations from `@stdlib/atomic` order memory as
   documented in [atomics.md](atomics.md) (sequentially consistent
   unless stated otherwise).

Nothing else orders anything. In particular, writing a shared binding in
one task and reading it in another — with no edge from the list between
the write and the read — is unordered, and (in the compiled backend) an
outright data race. Two `send`s on *different* channels are not ordered
with each other; `select()` observing channel A says nothing about
channel B.

## The race-free fragment (and its theorem)

Call a program **channel-disciplined** if:

- every value it passes between tasks travels via spawn arguments, task
  results, or channels — no task reads or writes a binding (global or
  captured) that another live task writes, no `ptr` or `buffer` is
  reachable from two tasks at once, and
- for every compound value sent on a channel, the sender performs no
  interior reads or writes after the send (ownership transfer), and the
  value's object graph is not otherwise reachable from the sender's roots.

**Theorem (race freedom).** A channel-disciplined program has no data
races: for any two conflicting accesses (same location, at least one a
write, different threads), one happens-before the other.

*Proof sketch.* Induct on the ownership of each heap object. An object is
born owned by the thread that allocated it (thread-confined). Confined
objects admit no cross-thread access, so no race. Ownership changes only
at the four sanctioned points, and each point carries a happens-before
edge covering the handoff: spawn deep-copies (the copy is born confined
to the new thread; the original stays with the spawner), join transfers
after task termination (edge 3), channel transfer is ordered by edge 4
and the discipline forbids the sender from touching the object afterward,
so post-transfer the object is again confined to exactly one thread.
Primitives are copied by value everywhere and cannot alias. Reference
counts are the one location mutated from both sides of a transfer
(sender's scope-exit release vs. receiver's use); they are atomic
read-modify-writes and synchronize themselves. Hence every conflicting
pair of accesses is separated by the edge that accompanied the ownership
transfer between them. ∎

The theorem is the reason "use channels" is not just a style preference
in Hemlock: it is the boundary of the guaranteed-sound fragment. Step
outside it and you are in the unsafe fragment below — allowed, but on
your own.

What the fragment does *not* promise: determinism. Multi-producer
channels interleave nondeterministically; `select()` picks
scheduler-dependently. Race-free ≠ reproducible.

## Shared bindings are NOT a communication channel

```hemlock
let counter = 0;

async fn work() {
    counter = counter + 1;   // DON'T: unsynchronized shared write
}
```

A spawned task can *see* the spawner's globals and captured variables
(functions carry their closure environment, uncopied). The temptation is
to treat that visibility as a communication mechanism. Don't. The
guarantees are deliberately weak, and they differ by backend:

- **Interpreter**: each individual read or write of a *binding* (the
  variable slot itself) is internally synchronized, so the interpreter
  will not corrupt its own state — you get "some interleaving," never a
  torn value. But `counter = counter + 1` is still two operations;
  concurrent increments lose updates. And the synchronization covers the
  slot only, **not** the interior of the array/object/string the slot
  points to: two tasks pushing to one shared array can corrupt it
  (element storage reallocates under a concurrent reader — use-after-free).
- **Compiled**: captured-variable cells are synchronized like the
  interpreter's. **Module globals are plain C globals** — a concurrent
  read and write is a C-level data race on a 16-byte value: torn type
  tags, dangling pointers, undefined behavior.

The rule that makes both backends agree — and the one this document
makes normative:

> **After the first `spawn`, a binding reachable from more than one live
> task must be treated as read-only. Any cross-task write to a shared
> binding, and any interior mutation of a compound value reachable from
> two tasks, is a data race** — unprotected by the language, regardless
> of backend. Communicate through channels and task results instead.

(The interpreter's per-slot locking is defense-in-depth for the
interpreter's own integrity, not a language guarantee. Programs relying
on it are outside the model and diverge under `hemlockc`.)

The safe uses of shared visibility are: builtins, function definitions,
constants, and configuration written before the first `spawn` and never
written again — all covered by the spawn happens-before edge.

## The unsafe fragment

Consistent with "a small, unsafe language for writing unsafe things
safely," Hemlock lets you leave the channel-disciplined fragment:

- **Shared `ptr` / FFI memory.** Tasks share raw pointers freely. You
  order accesses with channels, `join()`, or `@stdlib/atomic`, and you
  keep the allocation alive until every task is done with it
  (`join()` before `free()`; a detached task must own its memory or
  signal completion on a channel first).
- **`@stdlib/atomic`** provides atomic loads/stores/RMW/CAS and fences on
  native memory for building your own lock-free structures.
- **Shared `buffer` via channel aliasing.** Sending a buffer and keeping
  the alias gives two tasks the same bytes. Byte-level races on buffer
  contents won't corrupt the runtime (the bounds/metadata are not
  reallocated by writes), but the *data* is whatever the race makes it.
- **`mmap`**, shared file descriptors, process-shared state: kernel
  semantics apply, Hemlock adds nothing.

Undefined behavior in the unsafe fragment is process-level: a data race
on a compiled global or a use-after-free through a shared pointer can
crash or corrupt arbitrarily. There is no sandbox between tasks — they
are threads in one address space.

## Sharp edges and non-guarantees

Pinning down semantics means writing down the warts too:

- **`recv()` returning `null` is ambiguous** if you send `null` values:
  "closed and drained" and "someone sent null" are indistinguishable.
  Protocol fix: don't send bare `null`; send a sentinel object, or close
  the channel to signal end-of-stream.
- **`select()` returns `{ channel, value: null }` for a closed channel**
  in the polled set — immediately and repeatedly, even while other
  channels in the set are open and active. `select()` returns bare
  `null` only on timeout or when *all* channels are closed. A fan-in
  loop must therefore not treat every non-`null` result as data; since
  channels don't support identity comparison (no `==` on channels), the
  practical pattern is sentinel messages — producers send a done marker
  instead of closing (see async-concurrency.md's fan-in example).
- **`select()` polls.** It is implemented as an adaptive polling loop
  (50µs–1ms), not a blocking multi-wait: expect up to ~1ms latency and
  nonzero idle CPU, and no fairness guarantee across ready channels
  (earlier array positions win ties).
- **Unbuffered `send_timeout` can succeed "late."** If the receiver
  takes the value at the same instant the timeout expires, the send
  reports success (the value was delivered exactly once). `false` means
  the value was withdrawn and delivered zero times. Exactly-once, either
  way.
- **Strings are mutable** (`s[0] = 'H'`), so a string is an "interior
  mutation" hazard exactly like arrays and objects when aliased across
  tasks. String *methods* return new strings and are safe on a
  receiver's copy.
- **Task handles**: `join` and `detach` are one-shot and mutually
  exclusive; a second `join`, or `join` after `detach`, throws (in both
  backends). Exceptions from detached tasks are unobservable — handle
  them inside the task.
- **No global ordering across channels.** Each channel orders its own
  messages (FIFO for buffered channels); the model promises nothing
  about the interleaving of two channels' traffic.
- **`panic()` kills the process**, all tasks included, immediately — it
  is not scoped to the panicking task.

## Implementation notes (non-normative)

How the backends currently uphold the model — useful for contributors,
not part of the contract:

- **Atomic reference counts.** All runtime-managed types
  (string/array/object/buffer/function/task/channel/socket) use atomic
  retain/release in both backends, so ownership handoffs and scope exits
  on different threads never corrupt counts.
- **Channel internals.** One mutex + `not_empty`/`not_full` condvars per
  channel; buffered channels are a ring buffer under the mutex.
  Unbuffered channels rendezvous through a single staging slot guarded
  by the same mutex, plus a `rendezvous` condvar and a **generation
  counter**: a staged sender records the handoff generation and waits
  for it to advance. The counter exists because with multiple staged
  senders, "`sender_waiting == 0`" cannot distinguish "my value was
  consumed" from "another sender re-staged," and a `signal()` wakeup can
  reach the wrong sender (lost wakeup → deadlock); consumers bump the
  generation and `broadcast`. The mutex acquire/release provides the
  send→recv happens-before edge.
- **Environment locking (interpreter).** Every environment has a mutex;
  slot reads retain under the lock, writes release-then-retain under the
  lock. Property-access inline caches on shared AST nodes are bypassed
  once the first task has ever been spawned (a monotonic global flag),
  because the IC itself would otherwise be cross-thread mutable state.
- **Closure cells (compiled).** `hml_closure_env_get/set` lock a
  per-environment mutex, mirroring the interpreter. Module globals are
  emitted as plain C globals with no locking — which is why the model
  refuses to bless cross-task global writes rather than pretending both
  backends synchronize them.
- **Task lifecycle.** Task structs are refcounted; the worker thread
  holds a reference for its lifetime, so fire-and-forget spawns can't
  use-after-free the task. Result and exception values are published
  under the task mutex and collected by `join` after `pthread_join` (or
  the completion condvar in the compiled runtime), which supplies edge 3.
  Worker threads run with all signals blocked; signal handlers execute
  on the main thread only.
- **Spawn deep copy** is performed by the *spawning* thread before
  `pthread_create`, so the copy itself needs no synchronization and the
  new thread starts with edge 2 covering everything it can see.

### Parity checklist for new concurrency features

Any change to spawn/join/channels must keep both backends aligned on:
argument taxonomy (table above), the six happens-before edges, exception
propagation on join, close/drain semantics, and the sharp edges list —
and must add a parity test under `tests/parity/` exercising the
cross-thread behavior (see `channel_unbuffered_multi_sender.hml` and
`channel_send_timeout_concurrent.hml` for the pattern of
scheduler-insensitive assertions: check sums and success flags, not
orderings).
