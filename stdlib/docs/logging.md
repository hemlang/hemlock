# @stdlib/logging - Logging Module

Comprehensive logging facilities for Hemlock applications with support for log levels, structured logging, file output, and filtering.

## Table of Contents

- [Overview](#overview)
- [Log Levels](#log-levels)
- [Logger Factory](#logger-factory)
- [Logging Methods](#logging-methods)
- [Configuration](#configuration)
- [Structured Logging](#structured-logging)
- [Default Logger](#default-logger)
- [Examples](#examples)
- [Best Practices](#best-practices)

---

## Overview

The logging module provides a flexible and powerful logging system for Hemlock applications. It supports:

- **Four log levels** (DEBUG, INFO, WARN, ERROR)
- **Structured logging** with key-value pairs (JSON)
- **Output to stdout or file**
- **Configurable formatting**
- **Log level filtering**
- **Timestamps** (Unix epoch format)

```hemlock
import { Logger, DEBUG, INFO, WARN, ERROR } from "@stdlib/logging";

let logger = Logger({ level: INFO, output: "app.log" });
logger.info("Application started");
logger.warn("Low memory warning");
logger.error("Database connection failed");
```

---

## Log Levels

The module provides four standard log levels as constants:

### Constants

- **`DEBUG = 0`** - Detailed diagnostic information
- **`INFO = 1`** - General informational messages
- **`WARN = 2`** - Warning messages (potential issues)
- **`ERROR = 3`** - Error messages (serious problems)

### Level Hierarchy

Log levels form a hierarchy where higher levels are more severe:

```
DEBUG < INFO < WARN < ERROR
  0      1      2      3
```

When you set a minimum log level, messages below that level are filtered out.

**Example:**
```hemlock
import { Logger, DEBUG, INFO, WARN, ERROR } from "@stdlib/logging";

let logger = Logger({ level: WARN });  // Only WARN and ERROR

logger.debug("This is filtered");      // Not shown
logger.info("This is filtered");       // Not shown
logger.warn("This is shown");          // ✓ Shown
logger.error("This is shown");         // ✓ Shown
```

---

## Logger Factory

### `Logger(config?)`

Create a logger instance with optional configuration.

**Parameters:**
- `config` (object, optional) - Configuration options

**Configuration Options:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `output` | string | `"stdout"` | Output target: `"stdout"` or file path |
| `level` | i32 | `INFO` | Minimum log level to output |
| `format` | string | `"{timestamp} [{level}] {message}"` | Log message format |
| `include_timestamp` | bool | `true` | Whether to include timestamps |

**Returns:** Logger object

**Format String Placeholders:**
- `{timestamp}` - Unix timestamp
- `{level}` - Log level name (DEBUG, INFO, WARN, ERROR)
- `{message}` - Log message content

**Example:**
```hemlock
import { Logger, DEBUG } from "@stdlib/logging";

// Basic logger (stdout, INFO level)
let logger1 = Logger();

// Custom configuration
let logger2 = Logger({
    output: "/var/log/app.log",
    level: DEBUG,
    format: "[{level}] {message}",
    include_timestamp: false
});

// Don't forget to close file loggers!
defer logger2.close();
```

---

## Logging Methods

Logger objects provide five methods for logging messages:

### `logger.debug(message, data?)`

Log a DEBUG level message.

**Parameters:**
- `message` (any) - Message to log (automatically converted to string)
- `data` (object, optional) - Structured data (key-value pairs)

**Returns:** null

**Example:**
```hemlock
logger.debug("Variable value", { x: 42, y: 100 });
```

---

### `logger.info(message, data?)`

Log an INFO level message.

**Parameters:**
- `message` (any) - Message to log
- `data` (object, optional) - Structured data

**Returns:** null

**Example:**
```hemlock
logger.info("Server started", { port: 8080, pid: 12345 });
```

---

### `logger.warn(message, data?)`

Log a WARN level message.

**Parameters:**
- `message` (any) - Message to log
- `data` (object, optional) - Structured data

**Returns:** null

**Example:**
```hemlock
logger.warn("High CPU usage", { cpu: 95.5, threshold: 80.0 });
```

---

### `logger.error(message, data?)`

Log an ERROR level message.

**Parameters:**
- `message` (any) - Message to log
- `data` (object, optional) - Structured data

**Returns:** null

**Example:**
```hemlock
logger.error("Database error", { code: 500, message: "Connection timeout" });
```

---

### `logger.log(level, message, data?)`

Log a message with an explicit log level.

**Parameters:**
- `level` (i32) - Log level constant (DEBUG, INFO, WARN, ERROR)
- `message` (any) - Message to log
- `data` (object, optional) - Structured data

**Returns:** null

**Example:**
```hemlock
import { DEBUG } from "@stdlib/logging";

let severity = DEBUG;
logger.log(severity, "Custom level logging");
```

---

## Configuration

### `logger.set_level(level)`

Change the minimum log level dynamically.

**Parameters:**
- `level` (i32) - New minimum log level

**Returns:** null

**Example:**
```hemlock
import { Logger, DEBUG, INFO, ERROR } from "@stdlib/logging";

let logger = Logger({ level: INFO });

logger.debug("Not shown");   // Filtered
logger.info("Shown");        // ✓

// Change to DEBUG level
logger.set_level(DEBUG);

logger.debug("Now shown");   // ✓
logger.info("Still shown");  // ✓

// Change to ERROR only
logger.set_level(ERROR);

logger.info("Not shown");    // Filtered
logger.warn("Not shown");    // Filtered
logger.error("Shown");       // ✓
```

---

### `logger.close()`

Close the file handle if using file output. Safe to call multiple times.

**Returns:** null

**Example:**
```hemlock
let logger = Logger({ output: "app.log" });

// ... use logger ...

logger.close();  // Close file when done
```

**Best Practice:** Always close file loggers to prevent resource leaks:

```hemlock
let logger = Logger({ output: "app.log" });
defer logger.close();  // Automatic cleanup

// ... use logger ...
```

---

## Structured Logging

Structured logging allows you to attach machine-readable key-value data to log messages.

### Basic Structured Logging

```hemlock
import { Logger, INFO } from "@stdlib/logging";

let logger = Logger();

// Log with structured data
logger.info("User login", {
    user_id: 12345,
    username: "alice",
    ip_address: "192.168.1.100",
    timestamp: 1638360000
});

// Output: 1638360123 [INFO] User login {"user_id":12345,"username":"alice","ip_address":"192.168.1.100","timestamp":1638360000}
```

### Nested Objects

```hemlock
logger.error("API request failed", {
    endpoint: "/api/users",
    status: 500,
    error: {
        code: "TIMEOUT",
        message: "Request timed out after 30s",
        retry_count: 3
    }
});

// Output includes nested JSON
```

### Arrays in Structured Data

```hemlock
logger.warn("Multiple validation errors", {
    field: "email",
    errors: ["Invalid format", "Domain not allowed", "Too long"]
});
```

---

## Default Logger

The module provides a default logger instance and convenience functions for quick logging:

### Default Logger Instance

```hemlock
import { default_logger } from "@stdlib/logging";

default_logger.info("Quick log message");
```

### Convenience Functions

```hemlock
import { debug, info, warn, error, log } from "@stdlib/logging";
import { DEBUG } from "@stdlib/logging";

// These use the default logger internally
debug("Debug message");
info("Info message");
warn("Warning message");
error("Error message");
log(DEBUG, "Custom level");

// With structured data
info("User action", { action: "click", button: "submit" });
```

**Note:** The default logger outputs to stdout with INFO level.

---

## Examples

### Example 1: Basic Console Logging

```hemlock
import { Logger, INFO } from "@stdlib/logging";

let logger = Logger();

logger.info("Application starting...");
logger.info("Loading configuration");
logger.info("Server ready on port 8080");
```

**Output:**
```
1638360123 [INFO] Application starting...
1638360124 [INFO] Loading configuration
1638360125 [INFO] Server ready on port 8080
```

---

### Example 2: File Logging with Error Handling

```hemlock
import { Logger, ERROR } from "@stdlib/logging";

let logger = Logger({
    output: "/var/log/myapp.log",
    level: ERROR
});
defer logger.close();

try {
    risky_operation();
} catch (e) {
    logger.error("Operation failed", {
        error: e,
        timestamp: __now(),
        context: "main_loop"
    });
}
```

---

### Example 3: Debug Logging During Development

```hemlock
import { Logger, DEBUG } from "@stdlib/logging";

let logger = Logger({ level: DEBUG });

fn process_data(data: array) {
    logger.debug("Processing started", { size: data.length });

    let i = 0;
    while (i < data.length) {
        logger.debug("Processing item", { index: i, value: data[i] });
        // ... process item ...
        i = i + 1;
    }

    logger.debug("Processing completed");
    return null;
}
```

---

### Example 4: Dynamic Log Level

```hemlock
import { Logger, DEBUG, INFO, getenv } from "@stdlib/logging";
import { getenv } from "@stdlib/env";

// Set log level from environment variable
let log_level = INFO;
let env_level = getenv("LOG_LEVEL");

if (env_level == "DEBUG") {
    log_level = DEBUG;
}

let logger = Logger({ level: log_level });

logger.debug("This shows only if LOG_LEVEL=DEBUG");
logger.info("This always shows");
```

---

### Example 5: HTTP Request Logging

```hemlock
import { Logger } from "@stdlib/logging";

let access_log = Logger({
    output: "access.log",
    format: "{timestamp} {message}"
});
defer access_log.close();

fn handle_request(request) {
    let start = __time_ms();

    // ... handle request ...

    let elapsed = __time_ms() - start;

    access_log.info("HTTP request", {
        method: request.method,
        path: request.path,
        status: 200,
        duration_ms: elapsed,
        ip: request.client_ip
    });
}
```

---

### Example 6: Multiple Loggers

```hemlock
import { Logger, DEBUG, ERROR } from "@stdlib/logging";

// Application logger (stdout, all levels)
let app_logger = Logger({ level: DEBUG });

// Error logger (file, errors only)
let error_logger = Logger({
    output: "errors.log",
    level: ERROR
});
defer error_logger.close();

fn process() {
    app_logger.debug("Starting process");

    try {
        // ... work ...
        app_logger.info("Process completed");
    } catch (e) {
        app_logger.error("Process failed");
        error_logger.error("Critical error", {
            error: e,
            timestamp: __now()
        });
    }
}
```

---

### Example 7: Custom Format

```hemlock
import { Logger } from "@stdlib/logging";

let logger = Logger({
    format: "[{level}] {message}",
    include_timestamp: false
});

logger.info("Clean output without timestamp");
// Output: [INFO] Clean output without timestamp

let logger2 = Logger({
    format: "{level}: {message}"
});

logger2.warn("Custom separator");
// Output: WARN: Custom separator
```

---

## Best Practices

### 1. Always Close File Loggers

Use `defer` to ensure cleanup:

```hemlock
let logger = Logger({ output: "app.log" });
defer logger.close();
```

### 2. Use Appropriate Log Levels

- **DEBUG**: Verbose diagnostic info (disabled in production)
- **INFO**: Normal application events
- **WARN**: Potentially harmful situations
- **ERROR**: Error events that might still allow the app to continue

### 3. Include Context with Structured Data

```hemlock
// Good: Provides context
logger.error("Database query failed", {
    query: "SELECT * FROM users",
    duration_ms: 5000,
    error: "Timeout"
});

// Less useful: No context
logger.error("Query failed");
```

### 4. Configure from Environment

```hemlock
import { getenv } from "@stdlib/env";

let log_level = INFO;
let env_level = getenv("LOG_LEVEL");
if (env_level == "DEBUG") {
    log_level = DEBUG;
}

let logger = Logger({ level: log_level });
```

### 5. Separate Concerns with Multiple Loggers

```hemlock
let app_logger = Logger({ output: "app.log" });
let access_logger = Logger({ output: "access.log" });
let error_logger = Logger({ output: "error.log", level: ERROR });

defer app_logger.close();
defer access_logger.close();
defer error_logger.close();
```

### 6. Avoid Logging Sensitive Data

```hemlock
// Bad: Logs password
logger.info("User login", { username: "alice", password: "secret123" });

// Good: Omit sensitive data
logger.info("User login", { username: "alice" });
```

---

## Implementation Details

### Format String Processing

The format string uses simple `replace()` operations for placeholder substitution. Custom format strings can use any combination of:
- `{timestamp}` - Unix epoch timestamp
- `{level}` - Log level name
- `{message}` - Log message

### Timestamp Format

Timestamps are currently Unix epoch seconds (i64). Future versions may support ISO 8601 formatting.

### Structured Data Serialization

Structured data is serialized to JSON using the object's `.serialize()` method. Circular references will throw an error.

### File Handling

- Files are opened in append mode (`"a"`)
- If file open fails, logger falls back to stdout
- File handles must be explicitly closed with `logger.close()`

---

## Future Enhancements

Planned improvements:
- ISO 8601 timestamp formatting
- Log rotation (size-based and time-based)
- Async logging (non-blocking file writes)
- Log compression
- Remote logging (syslog, HTTP endpoints)
- Log parsing utilities

---

## See Also

- [@stdlib/time](time.md) - Time functions used for timestamps
- [@stdlib/fs](fs.md) - File operations used for file logging
