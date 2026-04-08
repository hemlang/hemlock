# @stdlib/ipc - Inter-Process Communication

The `ipc` module provides facilities for communication between processes, including OS-level pipes and file-based mechanisms.

## Quick Start

```hemlock
import { Pipe, MessageQueue, Semaphore, SharedData } from "@stdlib/ipc";

// OS-level pipe for fast IPC
let p = Pipe();
p.send("Hello from writer");
let msg = p.recv();
print(msg);  // "Hello from writer"
p.close();

// Message queue for process communication
let mq = MessageQueue("/tmp/my_queue");
mq.send("Hello from process 1");

// In another process:
let msg = mq.recv();
print(msg);  // "Hello from process 1"

// Semaphore for synchronization
let sem = Semaphore("/tmp/my_sem", 1);
sem.acquire();
// ... critical section ...
sem.release();
```

## API Reference

### Pipe(): object

Create an OS-level pipe for inter-process communication. Returns a Pipe object wrapping a unidirectional data channel.

**Methods:**
- `send(data)` - Write a string to the pipe. Returns number of bytes written
- `recv(size?)` - Read up to `size` bytes (default 4096). Returns string or null on EOF
- `close_read()` - Close the read end
- `close_write()` - Close the write end
- `close()` - Close both ends

**Properties:**
- `read_fd: i32` - The read file descriptor
- `write_fd: i32` - The write file descriptor

```hemlock
import { Pipe } from "@stdlib/ipc";

let p = Pipe();

// Write data
p.send("Hello, Pipe!");

// Read data
let data = p.recv();
print(data);  // "Hello, Pipe!"

// Close when done
p.close();
```

### pipe_create(): object

Low-level pipe creation. Returns `{ read_fd: i32, write_fd: i32 }`.

```hemlock
import { pipe_create, fd_read, fd_write, fd_close } from "@stdlib/ipc";

let fds = pipe_create();
fd_write(fds.write_fd, "raw pipe data");
let data = fd_read(fds.read_fd, 4096);
fd_close(fds.read_fd);
fd_close(fds.write_fd);
```

### fd_read(fd, size?): string|null

Read up to `size` bytes from a file descriptor. Returns null on EOF.

### fd_write(fd, data): i32

Write a string to a file descriptor. Returns number of bytes written.

### fd_close(fd)

Close a file descriptor.

### dup2(oldfd, newfd): i32

Duplicate a file descriptor. Useful for redirecting stdin/stdout/stderr.

```hemlock
import { Pipe, dup2, STDOUT_FD } from "@stdlib/ipc";

let p = Pipe();
// Redirect stdout to pipe write end
dup2(p.write_fd, STDOUT_FD);
```

### Constants

- `STDIN_FD` - Standard input file descriptor (0)
- `STDOUT_FD` - Standard output file descriptor (1)
- `STDERR_FD` - Standard error file descriptor (2)

### MessageQueue(path): object

Create a file-based message queue for inter-process messaging.

**Parameters:**
- `path: string` - Directory path for the queue

**Methods:**
- `send(message)` - Send a message to the queue
- `recv(timeout_ms?)` - Receive a message (blocking, default 5000ms timeout)
- `try_recv()` - Receive without blocking (returns null if empty)
- `is_empty()` - Check if queue is empty
- `count()` - Get number of pending messages
- `clear()` - Clear all messages
- `destroy()` - Remove the queue

```hemlock
import { MessageQueue } from "@stdlib/ipc";

let mq = MessageQueue("/tmp/my_queue");

// Send messages
mq.send("Message 1");
mq.send("Message 2");

// Receive messages
let msg = mq.recv(1000);  // Wait up to 1 second
if (msg != null) {
    print("Got: " + msg);
}

// Non-blocking receive
let msg2 = mq.try_recv();
if (msg2 == null) {
    print("Queue empty");
}

// Cleanup
mq.destroy();
```

### Semaphore(path, initial?): object

Create a file-based counting semaphore for process synchronization.

**Parameters:**
- `path: string` - File path for the semaphore
- `initial: i32` - Initial value (default: 1)

**Methods:**
- `acquire(timeout_ms?)` - Decrement and block if zero (returns bool)
- `try_acquire()` - Try to decrement without blocking
- `release()` - Increment the semaphore
- `value()` - Get current value

```hemlock
import { Semaphore } from "@stdlib/ipc";

// Create semaphore with 3 permits
let sem = Semaphore("/tmp/my_sem", 3);

// Acquire permits
print(sem.value());      // 3
sem.acquire();
print(sem.value());      // 2

// Try non-blocking acquire
if (sem.try_acquire()) {
    print("Got permit");
    sem.release();  // Return permit
}

// Release all
sem.release();
print(sem.value());      // 3
```

### Mutex(path): object

Create a file-based mutex (binary semaphore) for mutual exclusion.

**Parameters:**
- `path: string` - File path for the mutex

**Methods:**
- `lock(timeout_ms?)` - Acquire the lock (returns bool)
- `try_lock()` - Try to acquire without blocking
- `unlock()` - Release the lock
- `is_locked()` - Check if locked

```hemlock
import { Mutex } from "@stdlib/ipc";

let mutex = Mutex("/tmp/my_mutex");

if (mutex.lock(1000)) {
    // Critical section
    print("In critical section");
    mutex.unlock();
} else {
    print("Could not acquire lock");
}
```

### SharedData(path): object

Create a shared key-value store backed by a file with locking.

**Parameters:**
- `path: string` - File path for the data

**Methods:**
- `get(key)` - Get a value (or null if not found)
- `set(key, value)` - Set a value (automatically locked)
- `delete(key)` - Delete a key
- `keys()` - Get all keys (array)
- `clear()` - Clear all data

```hemlock
import { SharedData } from "@stdlib/ipc";

let shared = SharedData("/tmp/shared_state");

// Set values (thread-safe)
shared.set("counter", 0);
shared.set("status", "running");

// Get values
let counter = shared.get("counter");
print("Counter: " + counter);

// Update atomically
let current = shared.get("counter");
shared.set("counter", current + 1);
```

### PidFile(path): object

Create a PID file for process identification and single-instance enforcement.

**Parameters:**
- `path: string` - Path for the PID file

**Methods:**
- `create()` - Create/write PID file (returns false if process already running)
- `read()` - Read PID from file (or null)
- `remove()` - Remove the PID file
- `is_owner()` - Check if current process owns the file

```hemlock
import { PidFile } from "@stdlib/ipc";

let pf = PidFile("/tmp/myapp.pid");

if (!pf.create()) {
    print("Application is already running!");
    exit(1);
}

// ... run application ...

// Cleanup on exit
pf.remove();
```

### Event(path): object

Create an event file for simple signaling between processes.

**Parameters:**
- `path: string` - Path for the event file

**Methods:**
- `set()` - Set the event (signal)
- `clear()` - Clear the event
- `is_set()` - Check if event is set
- `wait(timeout_ms?)` - Wait for event to be set

```hemlock
import { Event } from "@stdlib/ipc";

// Process 1: Wait for signal
let event = Event("/tmp/ready_signal");
print("Waiting for signal...");
if (event.wait(10000)) {
    print("Got signal!");
} else {
    print("Timeout waiting for signal");
}

// Process 2: Send signal
let event = Event("/tmp/ready_signal");
event.set();
print("Signal sent!");
```

## Examples

### Parent-Child Pipe Communication

```hemlock
import { Pipe, fd_close, fd_read, fd_write } from "@stdlib/ipc";
import { fork, wait } from "@stdlib/process";

let p = Pipe();

let pid = fork();
if (pid == 0) {
    // Child: close read end, write to parent
    p.close_read();
    p.send("Hello from child!");
    p.close_write();
    exit(0);
} else {
    // Parent: close write end, read from child
    p.close_write();
    let msg = p.recv();
    print("Parent received: " + msg);
    p.close_read();
    wait();
}
```

### Producer-Consumer Pattern

```hemlock
import { MessageQueue } from "@stdlib/ipc";

// Producer process
fn producer() {
    let mq = MessageQueue("/tmp/work_queue");

    let i = 0;
    while (i < 10) {
        mq.send("Task " + i);
        print("Sent task " + i);
        i = i + 1;
    }
    mq.send("DONE");  // Signal completion
}

// Consumer process
fn consumer() {
    let mq = MessageQueue("/tmp/work_queue");

    while (true) {
        let msg = mq.recv();
        if (msg == "DONE") {
            print("All tasks complete");
            break;
        }
        print("Processing: " + msg);
    }

    mq.destroy();
}
```

### Resource Pool

```hemlock
import { Semaphore } from "@stdlib/ipc";

let pool_size = 3;
let pool = Semaphore("/tmp/resource_pool", pool_size);

fn use_resource() {
    if (pool.acquire(5000)) {
        print("Got resource, pool now has " + pool.value() + " available");

        // Use the resource...
        sleep(1);

        pool.release();
        print("Released resource");
    } else {
        print("Timeout waiting for resource");
    }
}
```

### Single Instance Application

```hemlock
import { PidFile } from "@stdlib/ipc";
import { get_pid } from "@stdlib/process";

let pid_file = PidFile("/var/run/myapp.pid");

if (!pid_file.create()) {
    let existing_pid = pid_file.read();
    print("Error: Application already running with PID " + existing_pid);
    exit(1);
}

// Register cleanup
defer pid_file.remove();

print("Application started with PID " + get_pid());

// Main application logic...
```

### Shared State Coordination

```hemlock
import { SharedData, Mutex } from "@stdlib/ipc";

let state = SharedData("/tmp/app_state");
let mutex = Mutex("/tmp/app_state_lock");

fn update_counter() {
    mutex.lock();
    let count = state.get("counter");
    if (count == null) {
        count = 0;
    }
    state.set("counter", count + 1);
    print("Counter is now: " + state.get("counter"));
    mutex.unlock();
}
```

## Notes

- `Pipe()` uses OS-level pipes (`pipe()` syscall) for fast, in-memory IPC
- Pipes are unidirectional: one end writes, the other reads
- Reading from a pipe blocks until data is available or the write end is closed
- Close the write end of a pipe to signal EOF to the reader
- File-based mechanisms (MessageQueue, SharedData, etc.) persist across process restarts
- Clean up temporary IPC files when done to avoid clutter
- Timeout values are in milliseconds

## See Also

- [@stdlib/process](process.md) - Process management
- [@stdlib/async](async.md) - Async operations and channels
- [@stdlib/fs](fs.md) - File system operations
