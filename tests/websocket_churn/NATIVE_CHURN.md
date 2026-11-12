# Compiled WebSocket disconnect stress test

This integration test uses only Hemlock's WebSocket standard library and Python's
standard library. It does not need accounts, game code or third-party packages.
It targets the compiled native runtime, not the interpreter/FFI implementation.

From the repository root, after building the compiler and runtime:

```sh
make compiler
make -C runtime -j4
# hemlockc --runtime expects the archive at the runtime directory root.
ln -sf build/libhemlock_runtime.a runtime/libhemlock_runtime.a
./hemlockc --runtime "$PWD/runtime" \
  tests/websocket_churn/native_websocket_churn.hml -o /tmp/ws-churn
python3 tests/websocket_churn/run_native_websocket_churn.py \
  --server-exe /tmp/ws-churn --output /tmp/ws-churn-results \
  --rounds 30 --hold-seconds 2
```

Use `--port` to select an unused localhost port. The fixture sends a fresh
36,979-byte binary message (sequence plus 36,975 known bytes) roughly every 100ms
per peer. A healthy peer validates every byte and monotonically advancing sequence
while pairs of unread/slow peers repeatedly disconnect, alternating ordinary TCP
close and reset. The runner owns and terminates only its own server process.
Results record the executable digest, completed rounds, validated frames, errors
and whether the server survived before cleanup. This is bounded stress coverage,
not a leak, throughput, or long-session certification.

## Runtime behavior requiring review

Writes enqueue owned copies and run only in libwebsockets writable callbacks.
A successful send means queue acceptance, not remote delivery. RX and TX queues
each have an 8 MiB bound including node overhead. A failed or short write closes
the connection. Closed server connections discard unread messages; client-side
final-message draining is preserved.

Server connection handles remain stable until server teardown, with a hard cap
of 4,096 lifetime connections per server. This suits a bounded server lifecycle,
but maintainers should review or replace that policy for indefinitely reused
servers. Applications must join connection users before destroying the server.
