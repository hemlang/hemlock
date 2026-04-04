# Signal Handling in Hemlock

Hemlock provides **POSIX signal handling** for managing system signals like SIGINT (Ctrl+C), SIGTERM, and custom signals. This enables low-level process control and inter-process communication.

## Table of Contents

- [Overview](#overview)
- [Signal API](#signal-api)
- [Signal Constants](#signal-constants)
- [Basic Signal Handling](#basic-signal-handling)
- [Advanced Patterns](#advanced-patterns)
- [Signal Handler Behavior](#signal-handler-behavior)
- [Safety Considerations](#safety-considerations)
- [Common Use Cases](#common-use-cases)
- [Complete Examples](#complete-examples)

## Overview

Signal handling allows programs to:
- Respond to user interrupts (Ctrl+C, Ctrl+Z)
- Implement graceful shutdown
- Handle termination requests
- Use custom signals for inter-process communication
- Create alarm/timer mechanisms

**Important:** Signal handling is **inherently unsafe** in Hemlock's philosophy. Handlers can be called at any time, interrupting normal execution. The user is responsible for proper synchronization.

## Signal API

### signal(signum, handler_fn)

Register a signal handler function.

**Parameters:**
- `signum` (i32) - Signal number (constant like SIGINT, SIGTERM)
- `handler_fn` (function or null) - Function to call when signal is received, or `null` to reset to default

**Returns:** The previous handler function (or `null` if none)

**Example:**
```hemlock
import { signal, SIGINT } from "@stdlib/signal";

fn my_handler(sig) {
    print("Caught signal: " + typeof(sig));
}

let old_handler = signal(SIGINT, my_handler);
```

**Resetting to default:**
```hemlock
import { signal, SIGINT } from "@stdlib/signal";

signal(SIGINT, null);  // Reset SIGINT to default behavior
```

### raise(signum)

Send a signal to the current process.

**Parameters:**
- `signum` (i32) - Signal number to send

**Returns:** `null`

**Example:**
```hemlock
import { raise, SIGUSR1 } from "@stdlib/signal";

raise(SIGUSR1);  // Trigger SIGUSR1 handler
```

## Signal Constants

Hemlock provides standard POSIX signal constants as i32 values.

### Interrupt & Termination

| Constant | Value | Description | Common Trigger |
|----------|-------|-------------|----------------|
| `SIGINT` | 2 | Interrupt from keyboard | Ctrl+C |
| `SIGTERM` | 15 | Termination request | `kill` command |
| `SIGQUIT` | 3 | Quit from keyboard | Ctrl+\ |
| `SIGHUP` | 1 | Hangup detected | Terminal closed |
| `SIGABRT` | 6 | Abort signal | `abort()` function |

**Examples:**
```hemlock
import { signal, SIGINT, SIGTERM, SIGHUP } from "@stdlib/signal";

signal(SIGINT, handle_interrupt);   // Ctrl+C
signal(SIGTERM, handle_terminate);  // kill command
signal(SIGHUP, handle_hangup);      // Terminal closes
```

### User-Defined Signals

| Constant | Value | Description | Use Case |
|----------|-------|-------------|----------|
| `SIGUSR1` | 10 | User-defined signal 1 | Custom IPC |
| `SIGUSR2` | 12 | User-defined signal 2 | Custom IPC |

**Examples:**
```hemlock
import { signal, SIGUSR1, SIGUSR2 } from "@stdlib/signal";

// Use for custom communication
signal(SIGUSR1, reload_config);
signal(SIGUSR2, rotate_logs);
```

### Process Control

| Constant | Value | Description | Notes |
|----------|-------|-------------|-------|
| `SIGALRM` | 14 | Alarm clock timer | After `alarm()` |
| `SIGCHLD` | 17 | Child process status change | Process management |
| `SIGCONT` | 18 | Continue if stopped | Resume after SIGSTOP |
| `SIGSTOP` | 19 | Stop process | **Cannot be caught** |
| `SIGTSTP` | 20 | Terminal stop | Ctrl+Z |

**Examples:**
```hemlock
import { signal, SIGALRM, SIGCHLD } from "@stdlib/signal";

signal(SIGALRM, handle_timeout);
signal(SIGCHLD, handle_child_exit);
```

### I/O Signals

| Constant | Value | Description | When Sent |
|----------|-------|-------------|-----------|
| `SIGPIPE` | 13 | Broken pipe | Write to closed pipe |
| `SIGTTIN` | 21 | Background read from terminal | BG process reads TTY |
| `SIGTTOU` | 22 | Background write to terminal | BG process writes TTY |

**Examples:**
```hemlock
import { signal, SIGPIPE } from "@stdlib/signal";

signal(SIGPIPE, handle_broken_pipe);
```

## Basic Signal Handling

### Catching Ctrl+C

```hemlock
import { signal, SIGINT } from "@stdlib/signal";

let interrupted = false;

fn handle_interrupt(sig) {
    print("Caught SIGINT!");
    interrupted = true;
}

signal(SIGINT, handle_interrupt);

// Program continues running...
// User presses Ctrl+C -> handle_interrupt() is called

while (!interrupted) {
    // Do work...
}

print("Exiting due to interrupt");
```

### Handler Function Signature

Signal handlers receive one argument: the signal number (i32)

```hemlock
import { signal, SIGINT, SIGTERM } from "@stdlib/signal";

fn my_handler(signum) {
    print("Received signal: " + typeof(signum));
    // signum contains the signal number (e.g., 2 for SIGINT)

    if (signum == SIGINT) {
        print("This is SIGINT");
    }
}

signal(SIGINT, my_handler);
signal(SIGTERM, my_handler);  // Same handler for multiple signals
```

### Multiple Signal Handlers

Different handlers for different signals:

```hemlock
import { signal, SIGINT, SIGTERM, SIGUSR1 } from "@stdlib/signal";

fn handle_int(sig) {
    print("SIGINT received");
}

fn handle_term(sig) {
    print("SIGTERM received");
}

fn handle_usr1(sig) {
    print("SIGUSR1 received");
}

signal(SIGINT, handle_int);
signal(SIGTERM, handle_term);
signal(SIGUSR1, handle_usr1);
```

### Resetting to Default Behavior

Pass `null` as the handler to reset to default behavior:

```hemlock
import { signal, SIGINT } from "@stdlib/signal";

// Register custom handler
signal(SIGINT, my_handler);

// Later, reset to default (terminate on SIGINT)
signal(SIGINT, null);
```

### Raising Signals Manually

Send signals to your own process:

```hemlock
import { signal, raise, SIGUSR1 } from "@stdlib/signal";

let count = 0;

fn increment(sig) {
    count = count + 1;
}

signal(SIGUSR1, increment);

// Trigger handler manually
raise(SIGUSR1);
raise(SIGUSR1);

print(count);  // 2
```

## Advanced Patterns

### Graceful Shutdown Pattern

Common pattern for cleanup on termination:

```hemlock
import { signal, SIGINT, SIGTERM } from "@stdlib/signal";

let should_exit = false;

fn handle_shutdown(sig) {
    print("Shutting down gracefully...");
    should_exit = true;
}

signal(SIGINT, handle_shutdown);
signal(SIGTERM, handle_shutdown);

// Main loop
while (!should_exit) {
    // Do work...
    // Check should_exit flag periodically
}

print("Cleanup complete");
```

### Signal Counter

Track number of signals received:

```hemlock
import { signal, SIGUSR1 } from "@stdlib/signal";

let signal_count = 0;

fn count_signals(sig) {
    signal_count = signal_count + 1;
    print("Received " + typeof(signal_count) + " signals");
}

signal(SIGUSR1, count_signals);

// Later...
print("Total signals: " + typeof(signal_count));
```

### Configuration Reload on Signal

```hemlock
import { signal, SIGHUP } from "@stdlib/signal";

let config = load_config();

fn reload_config(sig) {
    print("Reloading configuration...");
    config = load_config();
    print("Configuration reloaded");
}

signal(SIGHUP, reload_config);  // Reload on SIGHUP

// Send SIGHUP to process to reload config
// From shell: kill -HUP <pid>
```

### Timeout Using SIGALRM

```hemlock
import { signal, SIGALRM } from "@stdlib/signal";

let timed_out = false;

fn handle_alarm(sig) {
    print("Timeout!");
    timed_out = true;
}

signal(SIGALRM, handle_alarm);

// Set alarm (not yet implemented in Hemlock, example only)
// alarm(5);  // 5 second timeout

while (!timed_out) {
    // Do work with timeout
}
```

### Signal-Based State Machine

```hemlock
import { signal, SIGUSR1, SIGUSR2 } from "@stdlib/signal";

let state = 0;

fn next_state(sig) {
    state = (state + 1) % 3;
    print("State: " + typeof(state));
}

fn prev_state(sig) {
    state = (state - 1 + 3) % 3;
    print("State: " + typeof(state));
}

signal(SIGUSR1, next_state);  // Advance state
signal(SIGUSR2, prev_state);  // Go back

// Control state machine:
// kill -USR1 <pid>  # Next state
// kill -USR2 <pid>  # Previous state
```

## Signal Handler Behavior

### Important Notes

**Handler Execution:**
- Handlers are called **synchronously** when the signal is received
- Handlers execute in the current process context
- Signal handlers share the closure environment of the function they're defined in
- Handlers can access and modify outer scope variables (like globals or captured variables)

**Best Practices:**
- Keep handlers simple and quick - avoid long-running operations
- Set flags rather than performing complex logic
- Avoid calling functions that might take locks
- Be aware that handlers can interrupt any operation

### What Signals Can Be Caught

**Can catch and handle:**
- SIGINT, SIGTERM, SIGUSR1, SIGUSR2, SIGHUP, SIGQUIT
- SIGALRM, SIGCHLD, SIGCONT, SIGTSTP
- SIGPIPE, SIGTTIN, SIGTTOU
- SIGABRT (but program will abort after handler returns)

**Cannot catch:**
- `SIGKILL` (9) - Always terminates process
- `SIGSTOP` (19) - Always stops process

**System-dependent:**
- Some signals have default behaviors that may differ by system
- Check your platform's signal documentation for specifics

### Handler Limitations

```hemlock
fn complex_handler(sig) {
    // Avoid these in signal handlers:

    // ❌ Long-running operations
    // process_large_file();

    // ❌ Blocking I/O
    // let f = open("log.txt", "a");
    // f.write("Signal received\n");

    // ❌ Complex state changes
    // rebuild_entire_data_structure();

    // ✅ Simple flag setting is safe
    let should_stop = true;

    // ✅ Simple counter updates are usually safe
    let signal_count = signal_count + 1;
}
```

## Safety Considerations

Signal handling is **inherently unsafe** in Hemlock's philosophy.

### Race Conditions

Handlers can be called at any time, interrupting normal execution:

```hemlock
import { signal, SIGUSR1 } from "@stdlib/signal";

let counter = 0;

fn increment(sig) {
    counter = counter + 1;  // Race condition if called during counter update
}

signal(SIGUSR1, increment);

// Main code also modifies counter
counter = counter + 1;  // Could be interrupted by signal handler
```

**Problem:** If signal arrives while main code is updating `counter`, the result is unpredictable.

### Async-Signal-Safety

Hemlock does **not** guarantee async-signal-safety:
- Handlers can call any Hemlock code (unlike C's restricted async-signal-safe functions)
- This provides flexibility but requires user caution
- Race conditions are possible if handler modifies shared state

### Best Practices for Safe Signal Handling

**1. Use Atomic Flags**

Simple boolean assignments are generally safe:

```hemlock
import { signal, SIGINT } from "@stdlib/signal";

let should_exit = false;

fn handler(sig) {
    should_exit = true;  // Simple assignment is safe
}

signal(SIGINT, handler);

while (!should_exit) {
    // work...
}
```

**2. Minimize Shared State**

```hemlock
let interrupt_count = 0;

fn handler(sig) {
    // Only modify this one variable
    interrupt_count = interrupt_count + 1;
}
```

**3. Defer Complex Operations**

```hemlock
import { signal, SIGHUP } from "@stdlib/signal";

let pending_reload = false;

fn signal_reload(sig) {
    pending_reload = true;  // Just set flag
}

signal(SIGHUP, signal_reload);

// In main loop:
while (true) {
    if (pending_reload) {
        reload_config();  // Do complex work here
        pending_reload = false;
    }

    // Normal work...
}
```

**4. Avoid Re-entrancy Issues**

```hemlock
let in_critical_section = false;
let data = [];

fn careful_handler(sig) {
    if (in_critical_section) {
        // Don't modify data while main code is using it
        return;
    }
    // Safe to proceed
}
```

## Common Use Cases

### 1. Graceful Server Shutdown

```hemlock
import { signal, SIGINT, SIGTERM } from "@stdlib/signal";

let running = true;

fn shutdown(sig) {
    print("Shutdown signal received");
    running = false;
}

signal(SIGINT, shutdown);
signal(SIGTERM, shutdown);

// Server main loop
while (running) {
    handle_client_request();
}

cleanup_resources();
print("Server stopped");
```

### 2. Configuration Reload (Without Restart)

```hemlock
import { signal, SIGHUP } from "@stdlib/signal";

let config = load_config("app.conf");
let reload_needed = false;

fn trigger_reload(sig) {
    reload_needed = true;
}

signal(SIGHUP, trigger_reload);

while (true) {
    if (reload_needed) {
        print("Reloading configuration...");
        config = load_config("app.conf");
        reload_needed = false;
    }

    // Use config...
}
```

### 3. Log Rotation

```hemlock
import { signal, SIGUSR1 } from "@stdlib/signal";

let log_file = open("app.log", "a");
let rotate_needed = false;

fn trigger_rotate(sig) {
    rotate_needed = true;
}

signal(SIGUSR1, trigger_rotate);

while (true) {
    if (rotate_needed) {
        log_file.close();
        // Rename old log, open new one
        exec("mv app.log app.log.old");
        log_file = open("app.log", "a");
        rotate_needed = false;
    }

    // Normal logging...
    log_file.write("Log entry\n");
}
```

### 4. Status Reporting

```hemlock
import { signal, SIGUSR1 } from "@stdlib/signal";

let requests_handled = 0;

fn report_status(sig) {
    print("Status: " + typeof(requests_handled) + " requests handled");
}

signal(SIGUSR1, report_status);

while (true) {
    handle_request();
    requests_handled = requests_handled + 1;
}

// From shell: kill -USR1 <pid>
```

### 5. Debug Mode Toggle

```hemlock
import { signal, SIGUSR2 } from "@stdlib/signal";

let debug_mode = false;

fn toggle_debug(sig) {
    debug_mode = !debug_mode;
    if (debug_mode) {
        print("Debug mode: ON");
    } else {
        print("Debug mode: OFF");
    }
}

signal(SIGUSR2, toggle_debug);

// From shell: kill -USR2 <pid> to toggle
```

## Complete Examples

### Example 1: Interrupt Handler with Cleanup

```hemlock
import { signal, raise, SIGINT, SIGUSR1 } from "@stdlib/signal";

let running = true;
let signal_count = 0;

fn handle_signal(signum) {
    signal_count = signal_count + 1;

    if (signum == SIGINT) {
        print("Interrupt detected (Ctrl+C)");
        running = false;
    }

    if (signum == SIGUSR1) {
        print("User signal 1 received");
    }
}

// Register handlers
signal(SIGINT, handle_signal);
signal(SIGUSR1, handle_signal);

// Simulate some work
let i = 0;
while (running && i < 100) {
    print("Working... " + typeof(i));

    // Trigger SIGUSR1 every 10 iterations
    if (i == 10 || i == 20) {
        raise(SIGUSR1);
    }

    i = i + 1;
}

print("Total signals received: " + typeof(signal_count));
```

### Example 2: Multi-Signal State Machine

```hemlock
import { signal, SIGUSR1, SIGUSR2, SIGHUP } from "@stdlib/signal";

let state = "idle";
let request_count = 0;

fn start_processing(sig) {
    state = "processing";
    print("State: " + state);
}

fn stop_processing(sig) {
    state = "idle";
    print("State: " + state);
}

fn report_stats(sig) {
    print("State: " + state);
    print("Requests: " + typeof(request_count));
}

signal(SIGUSR1, start_processing);
signal(SIGUSR2, stop_processing);
signal(SIGHUP, report_stats);

while (true) {
    if (state == "processing") {
        // Do work
        request_count = request_count + 1;
    }

    // Check every iteration...
}
```

### Example 3: Worker Pool Controller

```hemlock
import { signal, SIGUSR1, SIGUSR2, SIGINT, SIGTERM } from "@stdlib/signal";

let worker_count = 4;
let should_exit = false;

fn increase_workers(sig) {
    worker_count = worker_count + 1;
    print("Workers: " + typeof(worker_count));
}

fn decrease_workers(sig) {
    if (worker_count > 1) {
        worker_count = worker_count - 1;
    }
    print("Workers: " + typeof(worker_count));
}

fn shutdown(sig) {
    print("Shutting down...");
    should_exit = true;
}

signal(SIGUSR1, increase_workers);
signal(SIGUSR2, decrease_workers);
signal(SIGINT, shutdown);
signal(SIGTERM, shutdown);

// Main loop adjusts worker pool based on worker_count
while (!should_exit) {
    // Manage workers based on worker_count
    // ...
}
```

### Example 4: Timeout Pattern

```hemlock
import { signal, SIGALRM } from "@stdlib/signal";

let operation_complete = false;
let timed_out = false;

fn timeout_handler(sig) {
    timed_out = true;
}

signal(SIGALRM, timeout_handler);

// Start long operation
async fn long_operation() {
    // ... work
    operation_complete = true;
}

let task = spawn(long_operation);

// Wait with timeout (manual check)
let elapsed = 0;
while (!operation_complete && elapsed < 1000) {
    // Sleep or check
    elapsed = elapsed + 1;
}

if (!operation_complete) {
    print("Operation timed out");
    detach(task);  // Give up waiting
} else {
    join(task);
    print("Operation completed");
}
```

## Debugging Signal Handlers

### Add Diagnostic Prints

```hemlock
import { signal, SIGINT } from "@stdlib/signal";

fn debug_handler(sig) {
    print("Handler called for signal: " + typeof(sig));
    print("Stack: (not yet available)");

    // Your handler logic...
}

signal(SIGINT, debug_handler);
```

### Count Signal Calls

```hemlock
let handler_calls = 0;

fn counting_handler(sig) {
    handler_calls = handler_calls + 1;
    print("Handler call #" + typeof(handler_calls));

    // Your handler logic...
}
```

### Test with raise()

```hemlock
import { signal, raise, SIGUSR1 } from "@stdlib/signal";

fn test_handler(sig) {
    print("Test signal received: " + typeof(sig));
}

signal(SIGUSR1, test_handler);

// Test by manually raising
raise(SIGUSR1);
print("Handler should have been called");
```

## Summary

Hemlock's signal handling provides:

- ✅ POSIX signal handling for low-level process control
- ✅ 15 standard signal constants
- ✅ Simple signal() and raise() API
- ✅ Flexible handler functions with closure support
- ✅ Multiple signals can share handlers

Remember:
- Signal handling is inherently unsafe - use with caution
- Keep handlers simple and fast
- Use flags for state changes, not complex operations
- Handlers can interrupt execution at any time
- Cannot catch SIGKILL or SIGSTOP
- Test handlers thoroughly with raise()

Common patterns:
- Graceful shutdown (SIGINT, SIGTERM)
- Configuration reload (SIGHUP)
- Log rotation (SIGUSR1)
- Status reporting (SIGUSR1/SIGUSR2)
- Debug mode toggle (SIGUSR2)
