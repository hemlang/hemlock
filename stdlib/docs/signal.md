# Signal Constants Module (`@stdlib/signal`)

A standard library module providing POSIX signal constants for use with the `signal()` and `raise()` built-in functions.

## Overview

The signal module provides named constants for POSIX signals, organized into four categories:

- **Terminal signals** - Process termination and interruption (SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGABRT)
- **User-defined signals** - Application-specific signaling (SIGUSR1, SIGUSR2)
- **Miscellaneous signals** - Timers, child processes, pipes (SIGALRM, SIGCHLD, SIGPIPE)
- **Job control signals** - Process stop/continue and terminal I/O (SIGCONT, SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU)

## Usage

```hemlock
import { SIGINT, SIGTERM } from "@stdlib/signal";

signal(SIGINT, fn(sig) {
    print("Caught interrupt signal");
});
```

Or import all:

```hemlock
import * as sig from "@stdlib/signal";

signal(sig.SIGINT, fn(s) {
    print("Interrupted");
});
```

---

## Terminal Signals

### SIGINT
Interrupt signal, sent when the user presses **Ctrl+C**. Commonly used to implement graceful shutdown.

**Default behavior:** Terminate the process.

### SIGTERM
Termination request signal. Sent by `kill` command by default. The standard way to politely ask a process to exit.

**Default behavior:** Terminate the process.

### SIGHUP
Hangup signal, originally sent when a terminal connection was lost. Often used to request configuration reload in daemons.

**Default behavior:** Terminate the process.

### SIGQUIT
Quit signal, sent when the user presses **Ctrl+\\**. Similar to SIGINT but typically produces a core dump.

**Default behavior:** Terminate the process (with core dump).

### SIGABRT
Abort signal. Sent by the `abort()` C function or by `panic()` in Hemlock. Indicates an abnormal termination.

**Default behavior:** Terminate the process (with core dump).

---

## User-Defined Signals

### SIGUSR1
User-defined signal 1. Has no predefined meaning - available for application-specific purposes.

**Default behavior:** Terminate the process.

### SIGUSR2
User-defined signal 2. Has no predefined meaning - available for application-specific purposes.

**Default behavior:** Terminate the process.

---

## Miscellaneous Signals

### SIGALRM
Alarm signal, sent when a timer set by `alarm()` expires. Useful for implementing timeouts.

**Default behavior:** Terminate the process.

### SIGCHLD
Child process status changed. Sent to the parent when a child process stops or terminates.

**Default behavior:** Ignored.

### SIGPIPE
Broken pipe. Sent when writing to a pipe or socket whose reading end has been closed.

**Default behavior:** Terminate the process.

---

## Job Control Signals

### SIGCONT
Continue signal. Resumes a process that was stopped by SIGSTOP or SIGTSTP.

**Default behavior:** Continue the process if stopped; otherwise ignored.

### SIGSTOP
Stop signal. Suspends the process. **Cannot be caught or ignored** - the handler will not be called.

**Default behavior:** Stop the process (unconditional).

### SIGTSTP
Terminal stop signal, sent when the user presses **Ctrl+Z**. Unlike SIGSTOP, this signal can be caught.

**Default behavior:** Stop the process.

### SIGTTIN
Background read signal. Sent to a background process that attempts to read from the terminal.

**Default behavior:** Stop the process.

### SIGTTOU
Background write signal. Sent to a background process that attempts to write to the terminal.

**Default behavior:** Stop the process.

---

## Constants Reference

| Constant | Description | Catchable |
|----------|-------------|-----------|
| `SIGINT` | Interrupt (Ctrl+C) | Yes |
| `SIGTERM` | Termination request | Yes |
| `SIGHUP` | Hangup (terminal closed) | Yes |
| `SIGQUIT` | Quit (Ctrl+\\) | Yes |
| `SIGABRT` | Abort | Yes |
| `SIGUSR1` | User-defined signal 1 | Yes |
| `SIGUSR2` | User-defined signal 2 | Yes |
| `SIGALRM` | Alarm timer expired | Yes |
| `SIGCHLD` | Child process status changed | Yes |
| `SIGPIPE` | Broken pipe | Yes |
| `SIGCONT` | Continue if stopped | Yes |
| `SIGSTOP` | Stop process | **No** |
| `SIGTSTP` | Terminal stop (Ctrl+Z) | Yes |
| `SIGTTIN` | Background read from terminal | Yes |
| `SIGTTOU` | Background write to terminal | Yes |

---

## Examples

### Graceful Shutdown on Ctrl+C

```hemlock
import { SIGINT, SIGTERM } from "@stdlib/signal";

let running = true;

fn shutdown(sig) {
    print("Shutting down gracefully...");
    running = false;
}

signal(SIGINT, shutdown);
signal(SIGTERM, shutdown);

while (running) {
    // ... do work ...
}

print("Cleanup complete");
```

### Configuration Reload with SIGHUP

```hemlock
import { SIGHUP } from "@stdlib/signal";

let config = { timeout: 30, debug: false };

fn reload_config(sig) {
    print("Reloading configuration...");
    // Re-read config file
    config = { timeout: 60, debug: true };
    print("Configuration reloaded");
}

signal(SIGHUP, reload_config);

// Main loop - send SIGHUP to reload without restarting
while (true) {
    // ... serve requests using config ...
}
```

### Inter-Process Communication with SIGUSR1

```hemlock
import { SIGUSR1 } from "@stdlib/signal";
import { get_pid } from "@stdlib/env";

let event_count = 0;

signal(SIGUSR1, fn(sig) {
    event_count = event_count + 1;
    print("Event received! Total: " + event_count);
});

print("PID: " + get_pid());
print("Send SIGUSR1 to trigger events");

// Keep running, waiting for signals
while (true) {
    // ... do work ...
}
```

### Raising Signals

```hemlock
import { SIGUSR1, SIGUSR2 } from "@stdlib/signal";

let state = "idle";

signal(SIGUSR1, fn(sig) {
    state = "processing";
    print("State changed to: " + state);
});

signal(SIGUSR2, fn(sig) {
    state = "idle";
    print("State changed to: " + state);
});

// Send a signal to the current process
raise(SIGUSR1);   // Triggers the SIGUSR1 handler
print("Current state: " + state);

raise(SIGUSR2);   // Triggers the SIGUSR2 handler
print("Current state: " + state);
```

### Ignoring SIGPIPE for Network Servers

```hemlock
import { SIGPIPE } from "@stdlib/signal";

// Ignore broken pipe errors (common in network servers)
signal(SIGPIPE, fn(sig) {
    // Intentionally empty - ignore the signal
});

// Now writing to closed sockets won't crash the server
// ... server code ...
```

### Timeout with SIGALRM

```hemlock
import { SIGALRM } from "@stdlib/signal";

let timed_out = false;

signal(SIGALRM, fn(sig) {
    timed_out = true;
    print("Operation timed out!");
});

// Set an alarm for 5 seconds
raise(SIGALRM);  // For demonstration; in practice, use alarm() via FFI

if (timed_out) {
    print("Handling timeout...");
}
```

---

## Notes

- `signal()` and `raise()` are global built-in functions and do not need to be imported from this module. Only the signal constants (SIGINT, SIGTERM, etc.) require importing.
- Signal constants are integer values matching POSIX definitions on the host platform.
- SIGSTOP cannot be caught, blocked, or ignored. Registering a handler for SIGSTOP has no effect.
- Signal handlers should be kept short and simple. Avoid allocating memory or performing complex operations inside handlers.
- Signal handling interacts with async tasks - signals are delivered to the main thread.
- Added in v1.10.0. Previously, signal constants were available as global constants without importing.

---

## See Also

- **Environment module** (`@stdlib/env`) - Process control with `exit()` and `get_pid()`
- **Process module** (`@stdlib/process`) - Fork, exec, wait, kill
- **Async** - Structured concurrency with spawn/join

---

## License

Part of the Hemlock standard library.
