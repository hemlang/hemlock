# Hemlock Time Module

A standard library module providing time measurement and delay functions for Hemlock programs.

## Overview

The time module provides essential time-related operations:

- **Time measurement** - Get current time in seconds or milliseconds
- **CPU time tracking** - Measure CPU time used by process
- **Delays** - Sleep for specified duration with sub-second precision

## Usage

```hemlock
import { now, time_ms, sleep, clock } from "@stdlib/time";

let start = time_ms();
// ... do work ...
let elapsed = time_ms() - start;
print("Elapsed: " + typeof(elapsed) + "ms");
```

Or import all:

```hemlock
import * as time from "@stdlib/time";
let timestamp = time.now();
```

---

## Functions

### now()
Returns the current Unix timestamp (seconds since January 1, 1970 00:00:00 UTC).

**Parameters:** None

**Returns:** `i64` - Unix timestamp in seconds

**Use cases:**
- Getting current date/time as timestamp
- Measuring elapsed time in seconds
- Timestamping events

```hemlock
import { now } from "@stdlib/time";

let timestamp = now();
print("Current timestamp: " + typeof(timestamp));  // e.g., 1700000000

// Measure elapsed time
let start = now();
// ... do work ...
let end = now();
let seconds_elapsed = end - start;
print("Took " + typeof(seconds_elapsed) + " seconds");
```

### time_ms()
Returns the current time in milliseconds since Unix epoch.

**Parameters:** None

**Returns:** `i64` - Time in milliseconds

**Use cases:**
- High-precision time measurement
- Benchmarking code performance
- Timestamping with millisecond precision

```hemlock
import { time_ms } from "@stdlib/time";

let start = time_ms();
// ... do work ...
let end = time_ms();
let ms_elapsed = end - start;
print("Took " + typeof(ms_elapsed) + "ms");
```

**Note:** Provides millisecond precision, much more accurate than `now()` for timing operations.

### sleep(seconds)
Pauses execution for the specified number of seconds. Supports sub-second precision.

**Parameters:**
- `seconds: number` - Number of seconds to sleep (can be fractional)

**Returns:** `null`

**Use cases:**
- Adding delays between operations
- Rate limiting
- Polling with intervals
- Animation timing

```hemlock
import { sleep } from "@stdlib/time";

// Sleep for 1 second
sleep(1.0);

// Sleep for half a second (500ms)
sleep(0.5);

// Sleep for 100 milliseconds
sleep(0.1);

// Sleep for 10 milliseconds
sleep(0.01);
```

**Implementation note:** Uses `nanosleep()` internally for precise sub-second delays.

### clock()
Returns the CPU time used by the current process in seconds.

**Parameters:** None

**Returns:** `f64` - CPU time in seconds

**Use cases:**
- Measuring CPU usage
- Profiling code performance
- Distinguishing CPU time from wall-clock time

```hemlock
import { clock } from "@stdlib/time";

let cpu_start = clock();
// ... CPU-intensive work ...
let cpu_end = clock();
let cpu_time = cpu_end - cpu_start;
print("CPU time: " + typeof(cpu_time) + " seconds");
```

**Note:** Returns CPU time, not wall-clock time. If your process is waiting (I/O, sleep), CPU time won't increase.

---

## Examples

### Basic Timing

```hemlock
import { time_ms } from "@stdlib/time";

fn measure(task_name: string, fn: function): null {
    let start = time_ms();
    fn();
    let elapsed = time_ms() - start;
    print(task_name + " took " + typeof(elapsed) + "ms");
    return null;
}

measure("Heavy computation", fn() {
    let sum = 0;
    let i = 0;
    while (i < 1000000) {
        sum = sum + i;
        i = i + 1;
    }
});
```

### Rate Limiting

```hemlock
import { sleep } from "@stdlib/time";

fn rate_limited_task(): null {
    let i = 0;
    while (i < 10) {
        print("Processing item " + typeof(i));
        // Process item...

        // Wait 100ms between items (max 10 items/second)
        sleep(0.1);
        i = i + 1;
    }
    return null;
}

rate_limited_task();
```

### Polling with Timeout

```hemlock
import { now, sleep } from "@stdlib/time";

fn wait_for_condition(check: function, timeout_seconds: i32): bool {
    let start = now();

    while (true) {
        if (check()) {
            return true;  // Condition met
        }

        let elapsed = now() - start;
        if (elapsed >= timeout_seconds) {
            return false;  // Timeout
        }

        sleep(0.1);  // Poll every 100ms
    }
}

// Usage
let result = wait_for_condition(fn() {
    // Check some condition
    return false;  // Example: not ready yet
}, 30);

if (result) {
    print("Condition met!");
} else {
    print("Timeout!");
}
```

### Benchmarking CPU vs Wall Time

```hemlock
import { clock, time_ms, sleep } from "@stdlib/time";

fn benchmark(): null {
    let wall_start = time_ms();
    let cpu_start = clock();

    // CPU-intensive work
    let sum = 0;
    let i = 0;
    while (i < 10000000) {
        sum = sum + i;
        i = i + 1;
    }

    // I/O wait (doesn't use CPU)
    sleep(1.0);

    let wall_elapsed = time_ms() - wall_start;
    let cpu_elapsed = clock() - cpu_start;

    print("Wall time: " + typeof(wall_elapsed) + "ms");
    print("CPU time: " + typeof(cpu_elapsed * 1000.0) + "ms");
    print("Efficiency: " + typeof(cpu_elapsed * 1000.0 / wall_elapsed * 100.0) + "%");

    return null;
}

benchmark();
// Output might be:
// Wall time: 1245ms
// CPU time: 234ms
// Efficiency: 18.8%
```

### Simple Timer Class

```hemlock
import { time_ms } from "@stdlib/time";

fn Timer() {
    let start_time = 0;
    let is_running = false;

    return {
        start: fn() {
            start_time = time_ms();
            is_running = true;
            return null;
        },

        stop: fn(): i64 {
            if (!is_running) {
                return 0;
            }
            let elapsed = time_ms() - start_time;
            is_running = false;
            return elapsed;
        },

        elapsed: fn(): i64 {
            if (!is_running) {
                return 0;
            }
            return time_ms() - start_time;
        },

        reset: fn() {
            start_time = time_ms();
            return null;
        }
    };
}

// Usage
let timer = Timer();
timer.start();

// ... do work ...

print("Elapsed so far: " + typeof(timer.elapsed()) + "ms");

// ... more work ...

let total = timer.stop();
print("Total time: " + typeof(total) + "ms");
```

---

## Time Resolution

### Precision by Function

| Function | Resolution | Range | Type |
|----------|------------|-------|------|
| `now()` | 1 second | Years | i64 |
| `time_ms()` | 1 millisecond | ~292 million years | i64 |
| `clock()` | Implementation-dependent | Process lifetime | f64 |
| `sleep()` | Nanosecond (1e-9s) | Any | - |

### Practical Precision

- **`now()`**: Good for date/time operations, not for precise timing
- **`time_ms()`**: Excellent for benchmarking, precise to milliseconds
- **`clock()`**: Varies by system, typically microsecond or better
- **`sleep()`**: Accurate to ~1-10ms on most systems (OS scheduling dependent)

---

## Platform Notes

### Unix Timestamp Range

The Unix timestamp (returned by `now()`) is an `i64`:
- **Range:** ~292 billion years (won't overflow until year 292,277,026,596)
- **Epoch:** January 1, 1970 00:00:00 UTC
- **Year 2038 problem:** Not applicable (i64 is used, not i32)

### Sleep Accuracy

`sleep()` uses `nanosleep()` internally:
- **Requested vs actual:** May sleep slightly longer due to OS scheduling
- **Minimum sleep:** ~1-10ms on most systems (OS dependent)
- **Sub-millisecond:** Possible but may be rounded up by OS scheduler

### CPU Time (clock())

`clock()` behavior varies by platform:
- **Unix/Linux:** Measures CPU time of current process
- **Multi-threaded:** May include time from all threads
- **Precision:** Typically microseconds, but system-dependent

---

## Common Patterns

### Retry with Exponential Backoff

```hemlock
import { sleep } from "@stdlib/time";

fn retry_with_backoff(operation: function, max_attempts: i32): bool {
    let attempt = 0;
    let delay = 0.1;  // Start with 100ms

    while (attempt < max_attempts) {
        try {
            operation();
            return true;  // Success
        } catch (e) {
            print("Attempt " + typeof(attempt + 1) + " failed: " + e);
            if (attempt < max_attempts - 1) {
                print("Retrying in " + typeof(delay) + "s...");
                sleep(delay);
                delay = delay * 2.0;  // Double delay each time
            }
        }
        attempt = attempt + 1;
    }

    return false;  // All attempts failed
}
```

### Periodic Task Execution

```hemlock
import { now, sleep } from "@stdlib/time";

fn run_periodic(task: function, interval_seconds: i32, duration_seconds: i32): null {
    let start_time = now();
    let next_run = start_time;

    while (now() - start_time < duration_seconds) {
        if (now() >= next_run) {
            task();
            next_run = next_run + interval_seconds;
        }
        sleep(0.1);  // Check every 100ms
    }

    return null;
}

// Run task every 5 seconds for 30 seconds
run_periodic(fn() {
    print("Task executed at " + typeof(now()));
}, 5, 30);
```

---

## Future Enhancements

Planned additions to the time module:
- **Date/time formatting** - Convert timestamps to human-readable strings
- **Parsing** - Parse date strings to timestamps
- **Time zones** - Convert between time zones
- **Duration helpers** - `days()`, `hours()`, `minutes()` functions
- **High-resolution timer** - Nanosecond precision timing

---

## Testing

Run the time module tests:

```bash
# Run all time tests
make test | grep stdlib_time

# Or run individual test
./hemlock tests/stdlib_time/test_time.hml
```

---

## See Also

- **Math module** - Use `@stdlib/math` for mathematical operations
- **Concurrency** - Use `sleep()` with async/await for delays in concurrent tasks

---

## License

Part of the Hemlock standard library.
