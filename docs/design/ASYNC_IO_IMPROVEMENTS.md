# Async I/O Improvement Proposal

> A roadmap for enhancing Hemlock's async I/O capabilities while preserving the explicit, unsafe design philosophy.

## Executive Summary

Hemlock's current async implementation provides **true pthread-based parallelism** with channels for communication. However, I/O operations (file, network) are blocking, requiring a thread per concurrent I/O operation. This proposal outlines improvements to enable efficient async I/O while maintaining Hemlock's explicit design philosophy.

---

## Current State Analysis

### What Works Well

| Feature | Implementation | Status |
|---------|---------------|--------|
| Task spawning | `pthread_create()` | ✅ Solid |
| Task joining | `pthread_join()` | ✅ Solid |
| Task detaching | `pthread_detach()` | ✅ Solid |
| Buffered channels | Ring buffer + mutex + condvars | ✅ Solid |
| Exception propagation | Via `ExecutionContext` | ✅ Solid |
| Reference counting | Thread-safe ref_count | ✅ Solid |

### Current Limitations

| Limitation | Impact | Severity |
|------------|--------|----------|
| No async I/O | 1 thread per blocking I/O | High |
| No `select()` on channels | Can't multiplex channel reads | Medium |
| No work-stealing scheduler | Overhead for many short tasks | Medium |
| Unbuffered channels unsupported | Workaround required | Low |
| No timeouts on channel ops | Potential indefinite blocking | Medium |
| IPv4 only (sockets) | No IPv6 support | Low |

### Relevant Code Locations

```
src/interpreter/builtins/concurrency.c  - spawn/join/detach (377 lines)
src/interpreter/io/channel_methods.c    - send/recv/close (121 lines)
src/interpreter/io/file_methods.c       - file operations (396 lines)
src/interpreter/builtins/net.c          - socket operations (651 lines)
include/interpreter.h                   - Task, Channel, SocketHandle structs
```

---

## Proposed Improvements

### Priority 1: Non-Blocking I/O Foundation

**Goal:** Enable file and socket operations to be used with a polling mechanism without blocking threads.

#### 1.1 Socket Non-Blocking Mode

Add `set_nonblocking()` method to sockets:

```hemlock
let sock = socket_create(AF_INET, SOCK_STREAM, 0);
sock.set_nonblocking(true);  // Enable non-blocking mode

// Now recv() returns immediately with EAGAIN/EWOULDBLOCK if no data
let result = sock.recv(1024);
if (result == null) {
    // No data available, try again later
}
```

**Implementation in `net.c`:**

```c
// socket.set_nonblocking(enable: bool) -> null
Value socket_method_set_nonblocking(SocketHandle *sock, Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1 || args[0].type != VAL_BOOL) {
        return throw_runtime_error(ctx, "set_nonblocking() expects 1 boolean argument");
    }

    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (args[0].as.as_bool) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    fcntl(sock->fd, F_SETFL, flags);
    sock->nonblocking = args[0].as.as_bool;

    return val_null();
}
```

**Modify `recv()` for non-blocking:**

```c
ssize_t received = recv(sock->fd, data, size, 0);
if (received < 0) {
    if (sock->nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        free(data);
        return val_null();  // No data available (not an error)
    }
    free(data);
    return throw_runtime_error(ctx, "Failed to receive: %s", strerror(errno));
}
```

#### 1.2 Poll-Based I/O Multiplexing

Add a `poll()` builtin for waiting on multiple file descriptors:

```hemlock
import { poll, POLLIN, POLLOUT } from "@stdlib/io";

let sock1 = socket_create(AF_INET, SOCK_STREAM, 0);
let sock2 = socket_create(AF_INET, SOCK_STREAM, 0);

// Wait for any socket to become readable (timeout: 1000ms)
let ready = poll([
    { fd: sock1, events: POLLIN },
    { fd: sock2, events: POLLIN }
], 1000);

for (item in ready) {
    if (item.revents & POLLIN) {
        let data = item.fd.recv(1024);
        // Process data
    }
}
```

**Implementation (new file `src/interpreter/builtins/poll.c`):**

```c
#include <poll.h>

// poll(fds: array<{fd, events}>, timeout_ms: i32) -> array<{fd, revents}>
Value builtin_poll(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        return throw_runtime_error(ctx, "poll() expects 2 arguments");
    }

    Array *fds_arr = args[0].as.as_array;
    int timeout_ms = value_to_int(args[1]);

    // Build pollfd array
    struct pollfd *pfds = malloc(sizeof(struct pollfd) * fds_arr->length);
    for (int i = 0; i < fds_arr->length; i++) {
        Object *item = fds_arr->elements[i].as.as_object;
        Value fd_val = object_get(item, "fd");
        Value events_val = object_get(item, "events");

        pfds[i].fd = get_fd_from_value(fd_val);  // Extract fd from socket/file
        pfds[i].events = value_to_int(events_val);
        pfds[i].revents = 0;
    }

    int result = poll(pfds, fds_arr->length, timeout_ms);

    // Build result array with revents
    Array *result_arr = array_new(fds_arr->length);
    for (int i = 0; i < fds_arr->length; i++) {
        if (pfds[i].revents != 0) {
            Object *item = object_new(NULL, 2);
            object_set(item, "fd", fds_arr->elements[i]);
            object_set(item, "revents", val_i32(pfds[i].revents));
            array_push(result_arr, val_object(item));
        }
    }

    free(pfds);
    return val_array(result_arr);
}
```

---

### Priority 2: Channel Select

**Goal:** Enable waiting on multiple channels simultaneously.

```hemlock
let ch1 = channel(10);
let ch2 = channel(10);
let ch3 = channel(10);

// Wait for any channel to have data (blocking)
let result = select([ch1, ch2, ch3]);
// result = { channel: ch2, value: 42 }  (ch2 had data first)

// With timeout (milliseconds)
let result = select([ch1, ch2], 1000);
if (result == null) {
    print("Timeout - no channel ready");
}
```

**Implementation approach:**

```c
// select(channels: array<channel>, timeout_ms?: i32) -> { channel, value } | null
Value builtin_select(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1 || args[0].type != VAL_ARRAY) {
        return throw_runtime_error(ctx, "select() expects array of channels");
    }

    Array *channels = args[0].as.as_array;
    int timeout_ms = (num_args > 1) ? value_to_int(args[1]) : -1;  // -1 = infinite

    // Create shared condition variable for all channels
    pthread_mutex_t select_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t select_cond = PTHREAD_COND_INITIALIZER;

    // Register this select with all channels
    for (int i = 0; i < channels->length; i++) {
        Channel *ch = channels->elements[i].as.as_channel;
        channel_register_select(ch, &select_mutex, &select_cond);
    }

    // Wait loop
    struct timespec deadline;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
    }

    pthread_mutex_lock(&select_mutex);

    while (1) {
        // Check all channels for data
        for (int i = 0; i < channels->length; i++) {
            Channel *ch = channels->elements[i].as.as_channel;
            if (channel_has_data(ch)) {
                Value val = channel_recv_nonblocking(ch);

                // Unregister from all channels
                for (int j = 0; j < channels->length; j++) {
                    channel_unregister_select(channels->elements[j].as.as_channel);
                }

                pthread_mutex_unlock(&select_mutex);

                // Return { channel, value }
                Object *result = object_new(NULL, 2);
                object_set(result, "channel", channels->elements[i]);
                object_set(result, "value", val);
                return val_object(result);
            }
        }

        // Wait for any channel to signal
        int rc;
        if (timeout_ms > 0) {
            rc = pthread_cond_timedwait(&select_cond, &select_mutex, &deadline);
            if (rc == ETIMEDOUT) {
                // Unregister and return null
                for (int j = 0; j < channels->length; j++) {
                    channel_unregister_select(channels->elements[j].as.as_channel);
                }
                pthread_mutex_unlock(&select_mutex);
                return val_null();
            }
        } else {
            pthread_cond_wait(&select_cond, &select_mutex);
        }
    }
}
```

**Channel struct modification:**

```c
typedef struct {
    // ... existing fields ...

    // For select() support
    pthread_mutex_t *select_mutex;
    pthread_cond_t *select_cond;
} Channel;
```

---

### Priority 3: Timed Channel Operations

**Goal:** Allow channel operations with timeouts to prevent indefinite blocking.

```hemlock
let ch = channel(10);

// Send with timeout (1 second)
let sent = ch.send_timeout(value, 1000);
if (!sent) {
    print("Channel full, send timed out");
}

// Receive with timeout
let result = ch.recv_timeout(1000);
if (result == null) {
    print("Channel empty, recv timed out");
}
```

**Implementation:**

```c
// recv_timeout(timeout_ms: i32) -> value | null
if (strcmp(method, "recv_timeout") == 0) {
    if (num_args != 1 || !is_integer(args[0])) {
        return throw_runtime_error(ctx, "recv_timeout() expects integer timeout (ms)");
    }

    int timeout_ms = value_to_int(args[0]);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(mutex);

    while (ch->count == 0 && !ch->closed) {
        int rc = pthread_cond_timedwait(not_empty, mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(mutex);
            return val_null();  // Timeout
        }
    }

    // ... rest of recv logic ...
}
```

---

### Priority 4: Thread Pool with Work Stealing

**Goal:** Reduce thread overhead for many concurrent tasks.

**Current problem:**
- 1000 spawned tasks = 1000 OS threads
- Each thread has ~8KB stack + kernel overhead
- Thread creation: ~10-20μs each

**Proposed solution:**

```hemlock
// Option 1: Explicit thread pool
import { ThreadPool } from "@stdlib/async";

let pool = ThreadPool(8);  // 8 worker threads

async fn compute(n: i32): i32 { return n * n; }

// Submit to pool instead of spawn()
let future = pool.submit(compute, 42);
let result = future.get();

pool.shutdown();
```

```hemlock
// Option 2: Global runtime pool (transparent)
// spawn() automatically uses thread pool if task count > threshold

async fn work(n: i32): i32 { return n * n; }

// First 16 spawns create threads directly
// Beyond that, tasks are queued for the pool
let tasks = [];
let i = 0;
while (i < 1000) {
    tasks.push(spawn(work, i));
    i = i + 1;
}
```

**Implementation sketch:**

```c
typedef struct {
    pthread_t *threads;
    int num_threads;

    // Work queue (lock-free or mutex-protected)
    Task **queue;
    int queue_head;
    int queue_tail;
    int queue_capacity;

    pthread_mutex_t queue_mutex;
    pthread_cond_t work_available;

    int shutdown;
} ThreadPool;

// Worker thread function
static void* pool_worker(void *arg) {
    ThreadPool *pool = (ThreadPool*)arg;

    while (!pool->shutdown) {
        pthread_mutex_lock(&pool->queue_mutex);

        while (pool->queue_head == pool->queue_tail && !pool->shutdown) {
            pthread_cond_wait(&pool->work_available, &pool->queue_mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        // Dequeue task
        Task *task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;

        pthread_mutex_unlock(&pool->queue_mutex);

        // Execute task
        execute_task(task);
    }

    return NULL;
}
```

---

### Priority 5: Unbuffered Channels

**Goal:** Support synchronous rendezvous-style communication.

```hemlock
let ch = channel(0);  // Unbuffered (currently errors)

// Sender blocks until receiver is ready
async fn sender(ch) {
    ch.send(42);  // Blocks until someone calls recv()
}

// Receiver blocks until sender is ready
async fn receiver(ch) {
    let val = ch.recv();  // Blocks until someone calls send()
}
```

**Implementation:**

```c
if (ch->capacity == 0) {
    // Unbuffered: synchronous rendezvous
    pthread_mutex_lock(mutex);

    // Store value for receiver
    ch->pending_value = msg;
    ch->has_pending = 1;

    // Signal any waiting receiver
    pthread_cond_signal(not_empty);

    // Wait until receiver picks it up
    while (ch->has_pending && !ch->closed) {
        pthread_cond_wait(not_full, mutex);
    }

    pthread_mutex_unlock(mutex);
    return val_null();
}
```

---

### Priority 6: Async File I/O

**Goal:** Non-blocking file operations for high-concurrency scenarios.

**Option A: Thread-based async (simpler)**

```hemlock
import { async_read_file, async_write_file } from "@stdlib/fs";

// Returns immediately with a task handle
let read_task = async_read_file("large_file.txt");
let write_task = async_write_file("output.txt", data);

// Do other work...

// Wait for completion
let content = join(read_task);
join(write_task);
```

**Implementation:** Spawn internal thread for each async file op.

**Option B: io_uring integration (Linux, advanced)**

```c
#include <liburing.h>

typedef struct {
    struct io_uring ring;
    int ring_fd;
} AsyncIOContext;

// Submit async read
void async_read_submit(AsyncIOContext *ctx, int fd, void *buf, size_t len, off_t offset) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
    io_uring_prep_read(sqe, fd, buf, len, offset);
    io_uring_sqe_set_data(sqe, /* callback context */);
    io_uring_submit(&ctx->ring);
}

// Poll for completions
int async_read_poll(AsyncIOContext *ctx) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_peek_cqe(&ctx->ring, &cqe);
    if (ret == 0) {
        // Completion available
        int bytes_read = cqe->res;
        io_uring_cqe_seen(&ctx->ring, cqe);
        return bytes_read;
    }
    return -1;  // No completion yet
}
```

---

### Priority 7: IPv6 Support

**Goal:** Enable IPv6 sockets for modern network applications.

```hemlock
import { AF_INET6 } from "@stdlib/net";

let sock = socket_create(AF_INET6, SOCK_STREAM, 0);
sock.bind("::", 8080);  // Bind to all IPv6 interfaces
sock.listen(10);
```

**Implementation changes in `net.c`:**

```c
if (sock->domain == AF_INET6) {
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);

    if (strcmp(address, "::") == 0) {
        addr.sin6_addr = in6addr_any;
    } else {
        inet_pton(AF_INET6, address, &addr.sin6_addr);
    }

    bind(sock->fd, (struct sockaddr*)&addr, sizeof(addr));
}
```

---

## Implementation Roadmap

### Phase 1: Non-Blocking Foundation (Estimated effort: Medium)
1. Add `set_nonblocking()` to sockets
2. Update `recv()`/`send()` for non-blocking mode
3. Implement `poll()` builtin
4. Add `POLLIN`, `POLLOUT`, `POLLERR` constants
5. Tests for non-blocking I/O patterns

### Phase 2: Channel Enhancements (Estimated effort: Medium)
1. Implement `recv_timeout()` and `send_timeout()`
2. Implement `select()` for multiple channels
3. Add unbuffered channel support
4. Tests for timeout and select patterns

### Phase 3: Thread Pool (Estimated effort: High)
1. Design ThreadPool struct and API
2. Implement work queue with proper synchronization
3. Integrate with existing `spawn()`
4. Add `ThreadPool` to stdlib
5. Performance benchmarks

### Phase 4: Advanced I/O (Estimated effort: High)
1. `async_read_file()` / `async_write_file()` wrappers
2. Evaluate io_uring integration (Linux-specific)
3. IPv6 socket support
4. DNS async resolution

---

## Design Considerations

### Explicit Over Implicit

Following Hemlock's philosophy, these improvements should be **opt-in**:

```hemlock
// Explicit non-blocking (user must handle)
sock.set_nonblocking(true);
let data = sock.recv(1024);
if (data == null) {
    // User explicitly handles no-data case
}

// NOT: magical async/await that hides complexity
```

### Error Handling

Non-blocking operations should use clear error semantics:

```hemlock
// Option 1: Return null for "would block"
let data = sock.recv(1024);  // null if EAGAIN

// Option 2: Return tagged object
let result = sock.try_recv(1024);
if (result.error == "EAGAIN") { ... }
if (result.error == "ECONNRESET") { ... }
```

### Thread Safety

All new primitives must be thread-safe:
- `poll()` can be called from multiple threads on different fd sets
- `select()` on channels must not corrupt channel state
- Thread pool must handle concurrent task submission

---

## Testing Strategy

### Unit Tests

```hemlock
// tests/async/nonblocking_socket.hml
let sock = socket_create(AF_INET, SOCK_STREAM, 0);
sock.set_nonblocking(true);

// recv on non-connected socket should return null (not block)
let result = sock.recv(1024);
assert(result == null);

sock.close();
print("PASS: non-blocking recv");
```

### Integration Tests

```hemlock
// tests/async/poll_multiple_sockets.hml
async fn server(port: i32, ch) {
    let listener = socket_create(AF_INET, SOCK_STREAM, 0);
    listener.bind("127.0.0.1", port);
    listener.listen(1);
    ch.send("ready");

    let client = listener.accept();
    client.send("hello");
    client.close();
    listener.close();
}

// ... poll test with multiple connections
```

### Stress Tests

```hemlock
// tests/async/thread_pool_stress.hml
let pool = ThreadPool(4);

let tasks = [];
let i = 0;
while (i < 10000) {
    tasks.push(pool.submit(fn(n) { return n * n; }, i));
    i = i + 1;
}

// All 10000 tasks should complete
let i = 0;
while (i < 10000) {
    let result = tasks[i].get();
    assert(result == i * i);
    i = i + 1;
}

pool.shutdown();
print("PASS: thread pool handled 10000 tasks");
```

---

## Summary

| Feature | Priority | Effort | Impact |
|---------|----------|--------|--------|
| Non-blocking sockets | P1 | Medium | High |
| `poll()` builtin | P1 | Medium | High |
| Channel `select()` | P2 | Medium | Medium |
| Timed channel ops | P3 | Low | Medium |
| Thread pool | P4 | High | High |
| Unbuffered channels | P5 | Low | Low |
| Async file I/O | P6 | High | Medium |
| IPv6 support | P7 | Low | Low |

The highest-value improvements are **non-blocking sockets + poll()** (P1) and **channel select()** (P2), as they enable efficient I/O multiplexing without major architectural changes.

---

## Appendix: Current Code Quality Assessment

### concurrency.c (377 lines)
- **Strengths:** Clean separation, proper mutex usage, atomic task IDs
- **Issues:** None major; production-quality

### channel_methods.c (121 lines)
- **Strengths:** Proper condvar usage, clean error handling
- **Issues:** Unbuffered channels error instead of implementing rendezvous

### net.c (651 lines)
- **Strengths:** Comprehensive socket API, proper error messages
- **Issues:** IPv4 only, no non-blocking mode

### file_methods.c (396 lines)
- **Strengths:** Complete file API with binary support
- **Issues:** Pure blocking I/O (acceptable given threading model)

Overall, the async implementation is solid and follows best practices for pthread-based concurrency. The improvements proposed here are **additive** rather than corrective.
