# Hemlock Examples

A guided tour of Hemlock, ordered from "your first program" to "complete
applications." The folders are numbered so you can walk them in order, but feel
free to jump straight to whatever you want to learn.

Run any example with the interpreter:

```bash
./hemlock examples/01-basics/fibonacci.hml
```

Files whose names contain `error` are **intentionally broken** — they
demonstrate what failure looks like (e.g. out-of-bounds access) and are
expected to abort with an error.

---

## 01-basics — language fundamentals

Tiny, single-concept programs. Start here if you're new to Hemlock.

| File | What it teaches |
|------|-----------------|
| `42.hml` | The smallest possible program: a single expression |
| `bools.hml` | Booleans and the logical operators `&&`, `||`, `!` |
| `mixed_math.hml` | Arithmetic and automatic type promotion |
| `conversions.hml` | Converting between numeric and string types |
| `countdown.hml` | `while` loops |
| `fibonacci.hml` | Recursion |
| `functions_demo.hml` | Declaring functions: parameters, returns |
| `strings.hml` | String basics |
| `string_manip.hml` | The string methods (`split`, `trim`, `replace`, …) |
| `alltypes.hml` | A tour of every built-in type |
| `types_test.hml` | Optional type annotations and `typeof` |
| `io_demo.hml` | `print` / `read_line` and the I/O system |
| `range_check.hml` | Bounds checking done right (the safe case) |
| `range_error.hml` | Out-of-bounds access (intentionally fails) |
| `range_error2.hml` | Another out-of-bounds case (intentionally fails) |
| `range_error3.hml` | Another out-of-bounds case (intentionally fails) |

## 02-intermediate — multi-part programs & the standard library

Programs that combine several features, use `@stdlib` modules, and do real
work like data processing, manual memory management, and HTTP.

| File | What it teaches |
|------|-----------------|
| `cli_args.hml` | Parsing command-line arguments |
| `map_reduce.hml` | `map` / `filter` / `reduce` over arrays |
| `pipeline.hml` | Chaining transformations into a data pipeline |
| `memory_demo.hml` | Manual memory: `alloc`/`talloc`/`buffer`/`free` (annotated) |
| `conway_life.hml` | A 2D grid simulation — Conway's Game of Life (annotated) |
| `benchmark_sorting.hml` | Implementing and timing a sort |
| `defer_rube_goldberg.hml` | How `defer` ordering works |
| `http_client.hml` | Fetching a URL with `@stdlib/http` |
| `http_example.hml` | More `@stdlib/http` request patterns |
| `websocket_echo_client.hml` | A basic WebSocket client |

## 03-concurrency — async, threads & channels

Hemlock uses real OS threads. These examples show `spawn`/`join` and channels,
working up to the classic synchronization problems.

| File | What it teaches |
|------|-----------------|
| `parallel_primes.hml` | CPU-bound parallelism with `spawn`/`join` (annotated) |
| `parallel_pizza.hml` | Overlapping independent async work |
| `producer_consumer.hml` | The producer–consumer pattern over a channel |
| `barrier_sync.hml` | Coordinating parallel phases with a barrier |
| `dining_philosophers.hml` | Channels as semaphores + deadlock avoidance (annotated) |
| `signal_orchestra.hml` | Handling OS signals |

## 04-projects — complete programs

Larger, self-contained applications. Great for reading end-to-end once you're
comfortable with the basics.

| File | What it is |
|------|-----------|
| `adventure_of_jimmy.hml` | A text-adventure RPG — match expressions, object methods, enums |
| `snake.hml` | The classic Snake game (terminal) |
| `tetris.hml` | Falling-blocks puzzle game (terminal) |
| `ascii_aquarium.hml` | An animated ASCII fish tank |
| `cosmic_cat_cafe.hml` | A whimsical cat-cafe simulation |
| `hemloco.hml` | "Lego Loco" — a train-town building game |
| `memory_palace.hml` | A larger exploration of memory operations |
| `websocket_client_lws.hml` | WebSocket client (requires libwebsockets) |
| `websocket_server_lws.hml` | WebSocket echo server (requires libwebsockets) |

---

## Other directories

These aren't part of the numbered progression:

- **`annotations/`** — demonstrates compiler optimization annotations
  (`@hot`, `@pure`, `@warn_unused`, …). See its own `README_ANNOTATIONS.md`.
- **`multi_module/`** — a multi-file project showing imports and the bundler.
  Run with `./hemlock examples/multi_module/main.hml`.
- **`wasm-browser/`** — running Hemlock in the browser via WebAssembly.

---

## A suggested path

1. Read `01-basics/` top to bottom — each file is a few lines.
2. In `02-intermediate/`, study `memory_demo.hml` closely: manual memory is the
   one thing Hemlock makes you think about that most languages hide.
3. Work through `03-concurrency/` in table order; the problems get harder as
   you go, ending with the deadlock-avoiding dining philosophers.
4. Pick a program from `04-projects/` and read it end to end.
