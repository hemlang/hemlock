# Hemlock Environment Module

A standard library module providing environment variable and process control functions for Hemlock programs.

## Overview

The env module provides essential system interaction capabilities:

- **Environment variables** - Read, write, and delete environment variables
- **Process control** - Exit with status codes, get process ID

## Usage

```hemlock
import { getenv, setenv, exit } from "@stdlib/env";

let path = getenv("PATH");
if (path == null) {
    print("PATH not set");
    exit(1);
}
```

Or import all:

```hemlock
import * as env from "@stdlib/env";
let home = env.getenv("HOME");
```

---

## Environment Variable Functions

### getenv(name)
Retrieves the value of an environment variable.

**Parameters:**
- `name: string` - Name of the environment variable

**Returns:** `string | null` - Value of the variable, or `null` if not set

**Use cases:**
- Reading configuration from environment
- Detecting system paths (PATH, HOME, etc.)
- Checking feature flags

```hemlock
import { getenv } from "@stdlib/env";

// Get common environment variables
let home = getenv("HOME");
let path = getenv("PATH");
let user = getenv("USER");

if (home != null) {
    print("Home directory: " + home);
} else {
    print("HOME not set");
}

// Check if variable exists
let debug = getenv("DEBUG");
if (debug != null) {
    print("Debug mode enabled");
}
```

### setenv(name, value)
Sets or updates an environment variable.

**Parameters:**
- `name: string` - Name of the environment variable
- `value: string` - Value to set

**Returns:** `null`

**Use cases:**
- Setting configuration for child processes
- Modifying PATH dynamically
- Setting feature flags

```hemlock
import { setenv, getenv } from "@stdlib/env";

// Set a new environment variable
setenv("MY_APP_CONFIG", "/etc/myapp/config.json");

// Update existing variable
let path = getenv("PATH");
setenv("PATH", path + ":/usr/local/myapp/bin");

// Set debugging flag
setenv("DEBUG", "1");

// Verify
print("Config: " + getenv("MY_APP_CONFIG"));
```

**Note:** Changes only affect current process and child processes, not parent process.

### unsetenv(name)
Removes an environment variable.

**Parameters:**
- `name: string` - Name of the environment variable to remove

**Returns:** `null`

**Use cases:**
- Cleaning up temporary environment settings
- Removing sensitive data from environment
- Resetting configuration

```hemlock
import { setenv, unsetenv, getenv } from "@stdlib/env";

// Set a temporary variable
setenv("TEMP_VAR", "temporary");
print("Before: " + getenv("TEMP_VAR"));  // "temporary"

// Remove it
unsetenv("TEMP_VAR");
print("After: " + getenv("TEMP_VAR"));   // null
```

---

## Process Control Functions

### exit(code?)
Terminates the program with an optional exit code.

**Parameters:**
- `code: i32` (optional) - Exit code (default: 0)

**Returns:** Never returns (terminates process)

**Exit code conventions:**
- `0` - Success
- `1-255` - Error codes (1 for general errors, others application-specific)

**Use cases:**
- Graceful program termination
- Signaling success or failure to parent process
- Early exit on fatal errors

```hemlock
import { exit } from "@stdlib/env";

// Exit with success
exit(0);

// Exit with error
exit(1);

// Exit with default (0)
exit();
```

**Example - Configuration validation:**
```hemlock
import { getenv, exit } from "@stdlib/env";

fn validate_config(): null {
    let required_vars = ["API_KEY", "DATABASE_URL", "PORT"];

    let i = 0;
    while (i < required_vars.length) {
        let var_name = required_vars[i];
        let value = getenv(var_name);

        if (value == null) {
            print("Error: Required environment variable " + var_name + " is not set");
            exit(1);
        }

        i = i + 1;
    }

    print("Configuration valid");
    return null;
}

validate_config();
// Program continues only if all variables are set
```

### get_pid()
Returns the process ID of the current process.

**Parameters:** None

**Returns:** `i32` - Process ID (PID)

**Use cases:**
- Creating process-specific temporary files
- Logging with process identification
- Process management and monitoring

```hemlock
import { get_pid } from "@stdlib/env";

let pid = get_pid();
print("Current process ID: " + typeof(pid));

// Create process-specific temp file
let temp_file = "/tmp/myapp." + typeof(pid) + ".tmp";
print("Using temp file: " + temp_file);
```

---

## Examples

### Configuration from Environment

```hemlock
import { getenv } from "@stdlib/env";

fn get_config(): object {
    return {
        host: getenv("SERVER_HOST"),
        port: getenv("SERVER_PORT"),
        debug: getenv("DEBUG") != null,
        log_level: getenv("LOG_LEVEL")
    };
}

let config = get_config();

if (config.host == null) {
    config.host = "localhost";  // Default
}

if (config.port == null) {
    config.port = "8080";  // Default
}

print("Server: " + config.host + ":" + config.port);
print("Debug: " + typeof(config.debug));
```

### Environment-based Feature Flags

```hemlock
import { getenv } from "@stdlib/env";

fn is_enabled(feature_name: string): bool {
    let env_var = "ENABLE_" + feature_name.to_upper();
    let value = getenv(env_var);

    if (value == null) {
        return false;
    }

    // Check for truthy values
    return value == "1" || value == "true" || value == "yes";
}

if (is_enabled("NEW_UI")) {
    print("Using new UI");
} else {
    print("Using legacy UI");
}

if (is_enabled("BETA_FEATURES")) {
    print("Beta features enabled");
}
```

### Building PATH Dynamically

```hemlock
import { getenv, setenv } from "@stdlib/env";

fn add_to_path(directory: string): null {
    let current_path = getenv("PATH");

    if (current_path == null) {
        setenv("PATH", directory);
    } else {
        // Check if already in PATH
        if (!current_path.contains(directory)) {
            setenv("PATH", current_path + ":" + directory);
        }
    }

    return null;
}

// Add custom directories to PATH
add_to_path("/usr/local/myapp/bin");
add_to_path("/opt/tools/bin");

print("Updated PATH: " + getenv("PATH"));
```

### Process Identification

```hemlock
import { get_pid } from "@stdlib/env";
import { write_file } from "@stdlib/fs";

fn create_pid_file(app_name: string): null {
    let pid = get_pid();
    let pid_file = "/var/run/" + app_name + ".pid";

    write_file(pid_file, typeof(pid));
    print("PID file created: " + pid_file);

    return null;
}

fn remove_pid_file(app_name: string): null {
    let pid_file = "/var/run/" + app_name + ".pid";
    // Remove file (using fs module)
    return null;
}

// Usage in daemon/service
create_pid_file("myapp");
// ... run application ...
// remove_pid_file("myapp");  // On shutdown
```

### Temporary File Naming

```hemlock
import { get_pid } from "@stdlib/env";

fn create_temp_filename(prefix: string, extension: string): string {
    let pid = get_pid();
    let timestamp = now();  // From time module
    return "/tmp/" + prefix + "." + typeof(pid) + "." + typeof(timestamp) + extension;
}

let temp_log = create_temp_filename("myapp", ".log");
print("Temporary log: " + temp_log);
// e.g., "/tmp/myapp.12345.1700000000.log"
```

### Graceful Shutdown Handler

```hemlock
import { exit } from "@stdlib/env";
import { signal, SIGINT, SIGTERM } from "builtin";  // Signal handling

let should_exit = false;

fn handle_shutdown(sig: i32): null {
    print("Received signal " + typeof(sig) + ", shutting down...");
    should_exit = true;
    return null;
}

// Register signal handlers
signal(SIGINT, handle_shutdown);
signal(SIGTERM, handle_shutdown);

// Main loop
while (!should_exit) {
    // ... do work ...

    if (should_exit) {
        print("Performing cleanup...");
        // ... cleanup code ...
        exit(0);
    }
}
```

### Environment Variable Validator

```hemlock
import { getenv, exit } from "@stdlib/env";

define EnvVar {
    name: string,
    required: bool,
    default_value?: string,
}

fn validate_environment(vars: array): object {
    let config = {};
    let errors = [];

    let i = 0;
    while (i < vars.length) {
        let var_def = vars[i];
        let value = getenv(var_def.name);

        if (value == null) {
            if (var_def.required) {
                errors.push("Required variable " + var_def.name + " not set");
            } else if (var_def.default_value != null) {
                value = var_def.default_value;
            }
        }

        // Store in config object (simplified)
        print(var_def.name + " = " + (value == null ? "(not set)" : value));

        i = i + 1;
    }

    if (errors.length > 0) {
        let j = 0;
        while (j < errors.length) {
            print("Error: " + errors[j]);
            j = j + 1;
        }
        exit(1);
    }

    return config;
}

// Usage
let env_vars = [
    { name: "API_KEY", required: true },
    { name: "PORT", required: false, default_value: "8080" },
    { name: "DEBUG", required: false }
];

let config = validate_environment(env_vars);
```

---

## Common Environment Variables

### System Variables (Unix/Linux)

| Variable | Description | Example |
|----------|-------------|---------|
| `HOME` | User's home directory | `/home/username` |
| `PATH` | Executable search paths | `/usr/bin:/usr/local/bin` |
| `USER` | Current username | `john` |
| `SHELL` | Default shell | `/bin/bash` |
| `PWD` | Current working directory | `/home/user/project` |
| `TERM` | Terminal type | `xterm-256color` |
| `LANG` | Locale setting | `en_US.UTF-8` |
| `TZ` | Time zone | `America/New_York` |

### Common Application Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `DEBUG` | Enable debug mode | `1`, `true` |
| `LOG_LEVEL` | Logging verbosity | `info`, `debug`, `error` |
| `PORT` | Server port | `8080` |
| `HOST` | Server host | `0.0.0.0` |
| `DATABASE_URL` | Database connection | `postgres://...` |
| `API_KEY` | API authentication | `sk-...` |

---

## Security Considerations

### Sensitive Data

**⚠️ WARNING:** Environment variables are **NOT secure** for sensitive data:
- Visible in process listings (`ps`, `/proc/`)
- Inherited by child processes
- May be logged or exposed in error messages

**Best practices:**
```hemlock
import { getenv, unsetenv } from "@stdlib/env";

// Get sensitive value
let api_key = getenv("API_KEY");

// Use it...
authenticate(api_key);

// Clear it from environment
unsetenv("API_KEY");

// api_key variable still in memory, but not in environment
```

**Better alternatives for secrets:**
- Use configuration files with restricted permissions
- Use secret management services (Vault, AWS Secrets Manager)
- Prompt for sensitive input at runtime

### Input Validation

Always validate environment variable contents:
```hemlock
import { getenv } from "@stdlib/env";

fn get_port(): i32 {
    let port_str = getenv("PORT");

    if (port_str == null) {
        return 8080;  // Default
    }

    // Validate: should be numeric, in valid range
    let port: i32 = port_str;  // Type conversion (simplified)

    if (port < 1 || port > 65535) {
        print("Error: Invalid port " + typeof(port));
        return 8080;  // Fall back to default
    }

    return port;
}
```

---

## Platform Differences

### Unix/Linux vs Windows

| Feature | Unix/Linux | Windows |
|---------|------------|---------|
| Path separator | `:` | `;` |
| Variable syntax | `$VAR` | `%VAR%` |
| Case sensitivity | Yes | No |
| Common variables | HOME, USER | USERPROFILE, USERNAME |

**Cross-platform patterns:**
```hemlock
import { getenv } from "@stdlib/env";

fn get_home_directory(): string {
    // Try Unix first
    let home = getenv("HOME");
    if (home != null) {
        return home;
    }

    // Fall back to Windows
    let userprofile = getenv("USERPROFILE");
    if (userprofile != null) {
        return userprofile;
    }

    return "/tmp";  // Last resort
}
```

---

## Testing

Run the env module tests:

```bash
# Run all env tests
make test | grep stdlib_env

# Or run individual test
./hemlock tests/stdlib_env/test_env.hml
```

---

## See Also

- **Filesystem module** (`@stdlib/fs`) - File and directory operations
- **Signals** - Process signal handling (builtin functions)
- **Command-line arguments** - Use `args` global array

---

## License

Part of the Hemlock standard library.
