# Hemlock 3 · Concurrency

The deepest 3.0 changes are here, because this is where real consumers
hit walls:

- **gn.hml** could not implement the EventEmitter pattern at all —
  `spawn()` deep-copies every argument, so a callback can never mutate
  the server object that registered it. The whole framework architecture
  had to be inverted around a blocking internal loop.
- **gn.hml's** latest fix (use-after-free: closing a server while spawned
  recv tasks still ran) exists because nothing ties a task's lifetime to
  the resources it borrows.
- **Witchgrid** is a long-running daemon that absorbed multiple 2.5.x
  fixes for leaked pthreads, leaked accepted sockets, and registry data
  races.

Three additions address all of it. All are *explicit* constructs — no
hidden sharing, no magic.

---

## 1. Structured concurrency: `scope`

A `scope` block owns every task spawned (directly) inside it and joins
them all before the block exits — on normal exit, on `throw`, on `break`.

```hemlock
scope {
    let t1 = spawn(fetch_page, url1);
    let t2 = spawn(fetch_page, url2);
    let a = await t1;
    let b = await t2;
}   // any task not yet awaited is joined here

// Exception propagation: if a child task throws and is never awaited,
// the scope joins remaining tasks, then rethrows the first child error
// at the scope's closing brace.
```

Rules:

1. Tasks spawned inside a `scope` are owned by it. `await`/`join` inside
   the scope works as today; whatever remains unjoined is joined at `}`.
2. A child error surfaces at the closing brace (first error wins;
   subsequent ones are attached as `e.data.suppressed`).
3. `detach(task)` inside a scope is legal and *removes* the task from the
   scope — escaping is allowed, but it is a visible, greppable act.
4. Scopes nest. A function's body may be a scope (`fn serve() scope { … }`
   sugar is **not** included — write the block).
5. Top-level code outside any `scope` behaves exactly as 2.x: this is an
   opt-in structure, not a global semantic change. `hml lint` flags
   `spawn` outside any scope that is neither awaited nor detached (this
   exact pattern was 2.5.7's pthread leak).

### Resource tie-in

Handle types (file, socket, websocket, channel) gain a uniform rule:
**`close()` on a handle that live tasks in the current scope still hold
blocks until those tasks are joined** when called at scope exit via
`defer`. Combined with refcounted handles (already landed for sockets and
websockets in 2.5.x/2.6), the gn.hml use-after-free class becomes
unrepresentable in scoped code:

```hemlock
scope {
    let srv = ws_listen(3000);
    defer srv.close();           // runs after the scope joins its tasks
    spawn(recv_loop, srv);
    spawn(recv_loop, srv);
}
```

---

## 2. Explicit sharing: `shared()`

The 2.x rule — `spawn` deep-copies primitives *and* heap values, shares
only channels and native handles — stays the default. It is the right
default: isolation by copy is why Hemlock has no data-race story to
apologize for. 3.0 adds the explicit escape hatch:

```hemlock
let state = shared({ connections: 0, peak: 0 });

async fn handle(conn, state) {
    state.with(fn(s) {            // runs body under the value's mutex
        s.connections += 1;
        if (s.connections > s.peak) { s.peak = s.connections; }
    });
}

scope {
    loop {
        let conn = srv.accept();
        if (conn == null) { break; }
        spawn(handle, conn, state);    // shared handles pass by reference
    }
}
```

Semantics:

- `shared(value)` wraps a heap value in a mutex + atomic refcount and
  returns a `shared` handle (`typeof` → `"shared"`, new
  `TYPEID_SHARED`).
- `spawn` passes `shared` handles **by reference** (retain, no deep
  copy). Everything else still copies. The sharing decision is visible at
  the `shared(...)` construction site — explicit over implicit.
- `s.with(fn)` locks, runs the closure with the inner value, unlocks
  (including on throw), and returns the closure's return value. The inner
  value must not escape the closure; escaping references are runtime
  errors (the closure parameter is invalidated on unlock).
- `s.get()` returns a **deep copy** of the inner value (safe read
  snapshot); `s.set(v)` replaces it under lock. Sugar over `with`.
- No lock-free field access on `shared` — every access path is visibly
  synchronized. For lock-free integers, `@stdlib/atomic` already exists.

This single primitive is what gn.hml needs to write an EventEmitter: the
server object lives in a `shared`, recv tasks and user callbacks mutate
it under `with`.

---

## 3. Channel `select`

Event-driven code (gn.hml's recv loops, Witchgrid's watchdog) currently
cannot wait on multiple channels. 3.0 adds C-style `select`:

```hemlock
select {
    case msg = ctrl.recv() => {
        if (msg == "shutdown") { break; }
    }
    case pkt = net_ch.recv() => {
        dispatch(pkt);
    }
    case timeout(5000) => {        // milliseconds; at most one timeout arm
        heartbeat();
    }
}
```

- Blocks until one arm is ready; if several are ready, picks uniformly at
  random (fairness, matching Go's well-tested choice).
- `default => { … }` arm makes it non-blocking.
- Receiving from a closed channel selects that arm with `null` (matching
  existing `recv()`-on-closed semantics).
- Implementation: a shared condvar per select; channels maintain waiter
  lists. Both backends must match — parity tests with deterministic
  single-ready-arm cases; the multi-ready randomness is exercised by
  stress tests, not parity tests.

---

## 4. Smaller fixes folded into 3.0

- **`spawn` argument deep-copy cost** stays (it is the isolation
  guarantee), but `shared`, channels, and handles now cover every
  documented case where the copy was the problem rather than the point.
- **Task handles are refcounted and self-cleaning** (2.5.7's
  `pthread_detach` safety net) — now specified behavior, not a fix.
- **Type registries (`define`/`enum`/`type`) are read-mostly rwlocked**
  (landed 2.6) — specified.
- `task_debug_info()` reports scope ancestry, so `hml` tooling can render
  the task tree of a hung program.

---

## 5. Explicitly rejected

- **Async event loop / colored functions (JS model):** Hemlock's tasks
  are real OS threads; that simplicity is a selling point for systems
  scripting. Rejected.
- **Implicit sharing of all heap values (Java model):** would make every
  program racy by default and demolish the isolation story. Rejected.
- **Full actor model:** `shared` + channels compose into actors where
  wanted, without committing the language to a paradigm. Rejected as a
  core construct.
