# Hemlock OS Module

A standard library module providing platform detection, system information, and hardware details for Hemlock programs.

## Overview

The os module provides essential operating system interaction capabilities:

- **Platform detection** - Identify Linux, macOS, Windows, and CPU architecture
- **System info** - Hostname, username, home directory, temp directory
- **Hardware info** - CPU count, total memory, free memory
- **Convenience functions** - Platform checks, system info object, memory formatting

## Usage

```hemlock
import { platform, hostname, cpu_count } from "@stdlib/os";

print("Platform: " + platform());
print("Hostname: " + hostname());
print("CPU cores: " + typeof(cpu_count()));
```

Or import all:

```hemlock
import * as os from "@stdlib/os";

if (os.is_linux()) {
    print("Running on Linux");
}

let info = os.info();
print("Memory: " + os.format_bytes(info.total_memory));
```

---

## Platform Detection Functions

### platform()
Returns the operating system platform name.

**Parameters:** None

**Returns:** `string` - Platform name: `"linux"`, `"macos"`, `"windows"`, or `"unknown"`

**Use cases:**
- Conditional behavior based on OS
- Platform-specific file paths
- Feature availability checks

```hemlock
import { platform } from "@stdlib/os";

let p = platform();
print("Running on: " + p);  // e.g., "linux"

if (p == "linux") {
    print("Using Linux-specific features");
} else if (p == "macos") {
    print("Using macOS-specific features");
} else if (p == "windows") {
    print("Using Windows-specific features");
}
```

### arch()
Returns the CPU architecture.

**Parameters:** None

**Returns:** `string` - Architecture name (e.g., `"x86_64"`, `"aarch64"`, `"arm"`, `"i686"`)

**Use cases:**
- Loading platform-specific native libraries
- Optimizing for specific architectures
- System requirements validation

```hemlock
import { arch } from "@stdlib/os";

let a = arch();
print("CPU architecture: " + a);  // e.g., "x86_64"

if (a == "x86_64" || a == "amd64") {
    print("64-bit x86 system");
} else if (a == "aarch64" || a == "arm64") {
    print("64-bit ARM system");
}
```

### os_name()
Returns the OS kernel name.

**Parameters:** None

**Returns:** `string` - Kernel name (e.g., `"Linux"`, `"Darwin"`, `"Windows_NT"`)

```hemlock
import { os_name } from "@stdlib/os";

let name = os_name();
print("OS kernel: " + name);  // e.g., "Linux"
```

### os_version()
Returns the OS kernel version string.

**Parameters:** None

**Returns:** `string` - Version string (e.g., `"5.15.0-generic"`, `"22.1.0"`)

```hemlock
import { os_version } from "@stdlib/os";

let version = os_version();
print("Kernel version: " + version);  // e.g., "5.15.0-generic"
```

---

## Convenience Platform Checks

### is_linux()
Check if running on Linux.

**Returns:** `bool` - `true` if on Linux, `false` otherwise

```hemlock
import { is_linux } from "@stdlib/os";

if (is_linux()) {
    print("Linux-specific code here");
}
```

### is_macos()
Check if running on macOS.

**Returns:** `bool` - `true` if on macOS, `false` otherwise

```hemlock
import { is_macos } from "@stdlib/os";

if (is_macos()) {
    print("macOS-specific code here");
}
```

### is_windows()
Check if running on Windows.

**Returns:** `bool` - `true` if on Windows, `false` otherwise

```hemlock
import { is_windows } from "@stdlib/os";

if (is_windows()) {
    print("Windows-specific code here");
}
```

### is_unix()
Check if running on a Unix-like system (Linux or macOS).

**Returns:** `bool` - `true` if on Linux or macOS, `false` otherwise

```hemlock
import { is_unix } from "@stdlib/os";

if (is_unix()) {
    // Use Unix-style paths and commands
    let config_dir = homedir() + "/.config/myapp";
}
```

---

## System Information Functions

### hostname()
Returns the system hostname.

**Parameters:** None

**Returns:** `string` - System hostname

**Throws:** Exception if hostname cannot be determined

**Use cases:**
- Logging with host identification
- Distributed system identification
- Network configuration

```hemlock
import { hostname } from "@stdlib/os";

let host = hostname();
print("Running on: " + host);  // e.g., "myserver.example.com"
```

### username()
Returns the current username.

**Parameters:** None

**Returns:** `string` - Current user's username

**Throws:** Exception if username cannot be determined

**Use cases:**
- User-specific configuration paths
- Audit logging
- Multi-user applications

```hemlock
import { username } from "@stdlib/os";

let user = username();
print("Current user: " + user);  // e.g., "john"
```

### homedir()
Returns the user's home directory path.

**Parameters:** None

**Returns:** `string` - Home directory path

**Throws:** Exception if home directory cannot be determined

**Use cases:**
- User configuration files
- Default save locations
- Application data storage

```hemlock
import { homedir } from "@stdlib/os";

let home = homedir();
print("Home directory: " + home);  // e.g., "/home/john"

// Create user-specific paths
let config_file = home + "/.myapp/config.json";
let data_dir = home + "/.local/share/myapp";
```

### tmpdir()
Returns the system temporary directory path.

**Parameters:** None

**Returns:** `string` - Temporary directory path (e.g., `/tmp`)

**Use cases:**
- Temporary file creation
- Cache directories
- Intermediate processing files

```hemlock
import { tmpdir, get_pid } from "@stdlib/os";
import { get_pid } from "@stdlib/env";

let tmp = tmpdir();
print("Temp directory: " + tmp);  // e.g., "/tmp"

// Create unique temp file path
let temp_file = tmp + "/myapp." + typeof(get_pid()) + ".tmp";
```

### uptime()
Returns the system uptime in seconds.

**Parameters:** None

**Returns:** `i64` - Seconds since system boot

**Throws:** Exception if uptime cannot be determined (platform-dependent)

**Use cases:**
- System monitoring
- Health checks
- Performance logging

```hemlock
import { uptime } from "@stdlib/os";

let up = uptime();
let days = up / 86400;
let hours = (up % 86400) / 3600;
let minutes = (up % 3600) / 60;

print("System uptime: " + typeof(days) + " days, " + typeof(hours) + " hours, " + typeof(minutes) + " minutes");
```

---

## Hardware Information Functions

### cpu_count()
Returns the number of logical CPU cores (processors).

**Parameters:** None

**Returns:** `i32` - Number of logical CPUs (minimum 1)

**Use cases:**
- Thread pool sizing
- Parallel processing configuration
- System resource reporting

```hemlock
import { cpu_count } from "@stdlib/os";

let cores = cpu_count();
print("CPU cores: " + typeof(cores));  // e.g., 8

// Size thread pool based on available cores
let worker_count = cores - 1;  // Leave one for main thread
if (worker_count < 1) {
    worker_count = 1;
}
```

### total_memory()
Returns the total system memory in bytes.

**Parameters:** None

**Returns:** `i64` - Total memory in bytes

**Throws:** Exception if memory info cannot be determined

**Use cases:**
- Memory allocation decisions
- System requirements checks
- Resource monitoring

```hemlock
import { total_memory, format_bytes } from "@stdlib/os";

let total = total_memory();
print("Total memory: " + format_bytes(total));  // e.g., "16 GB"
```

### free_memory()
Returns the available/free system memory in bytes.

**Parameters:** None

**Returns:** `i64` - Available memory in bytes

**Throws:** Exception if memory info cannot be determined

**Note:** On Linux, this includes buffers/cache as "available" memory.

```hemlock
import { free_memory, format_bytes } from "@stdlib/os";

let free = free_memory();
print("Available memory: " + format_bytes(free));  // e.g., "8 GB"
```

---

## Convenience Functions

### info()
Returns a comprehensive system information object.

**Parameters:** None

**Returns:** `object` - Object containing all system information

```hemlock
import { info } from "@stdlib/os";

let sys = info();

print("Platform: " + sys.platform);
print("Architecture: " + sys.arch);
print("OS Name: " + sys.os_name);
print("OS Version: " + sys.os_version);
print("Hostname: " + sys.hostname);
print("Username: " + sys.username);
print("Home Dir: " + sys.homedir);
print("Temp Dir: " + sys.tmpdir);
print("CPU Cores: " + typeof(sys.cpu_count));
print("Total Memory: " + typeof(sys.total_memory));
print("Free Memory: " + typeof(sys.free_memory));
print("Uptime: " + typeof(sys.uptime) + " seconds");
```

**Object structure:**
```hemlock
{
    platform: string,      // "linux", "macos", "windows"
    arch: string,          // "x86_64", "aarch64", etc.
    os_name: string,       // "Linux", "Darwin", etc.
    os_version: string,    // Kernel version
    hostname: string,      // System hostname
    username: string,      // Current username
    homedir: string,       // Home directory path
    tmpdir: string,        // Temp directory path
    cpu_count: i32,        // Number of CPUs
    total_memory: i64,     // Total RAM in bytes
    free_memory: i64,      // Free RAM in bytes
    uptime: i64            // Uptime in seconds
}
```

### memory_info()
Returns memory information as an object.

**Parameters:** None

**Returns:** `object` - Object with total, free, and used memory in bytes

```hemlock
import { memory_info, format_bytes } from "@stdlib/os";

let mem = memory_info();

print("Total: " + format_bytes(mem.total));
print("Free: " + format_bytes(mem.free));
print("Used: " + format_bytes(mem.used));

// Calculate usage percentage
let usage = (mem.used * 100) / mem.total;
print("Usage: " + typeof(usage) + "%");
```

### format_bytes(bytes)
Formats a byte count as a human-readable string.

**Parameters:**
- `bytes: i64` - Number of bytes

**Returns:** `string` - Human-readable string (e.g., `"1024 KB"`, `"2 GB"`)

```hemlock
import { format_bytes } from "@stdlib/os";

print(format_bytes(1024));           // "1 KB"
print(format_bytes(1048576));        // "1 MB"
print(format_bytes(1073741824));     // "1 GB"
print(format_bytes(1099511627776));  // "1 TB"
print(format_bytes(500));            // "500 B"
```

---

## Examples

### System Requirements Check

```hemlock
import * as os from "@stdlib/os";

fn check_requirements(): bool {
    let errors = [];

    // Check platform
    if (!os.is_linux() && !os.is_macos()) {
        errors.push("This application requires Linux or macOS");
    }

    // Check CPU cores
    let cores = os.cpu_count();
    if (cores < 2) {
        errors.push("At least 2 CPU cores required, found " + typeof(cores));
    }

    // Check memory (require at least 2 GB)
    let min_memory: i64 = 2147483648;  // 2 GB in bytes
    let total = os.total_memory();
    if (total < min_memory) {
        errors.push("At least 2 GB RAM required, found " + os.format_bytes(total));
    }

    // Report results
    if (errors.length > 0) {
        print("System requirements not met:");
        let i = 0;
        while (i < errors.length) {
            print("  - " + errors[i]);
            i = i + 1;
        }
        return false;
    }

    print("System requirements satisfied");
    return true;
}

check_requirements();
```

### Cross-Platform Configuration Paths

```hemlock
import { platform, homedir } from "@stdlib/os";

fn get_config_dir(app_name: string): string {
    let p = platform();
    let home = homedir();

    if (p == "linux") {
        // Follow XDG Base Directory spec
        return home + "/.config/" + app_name;
    } else if (p == "macos") {
        return home + "/Library/Application Support/" + app_name;
    } else if (p == "windows") {
        // On Windows, use APPDATA
        return home + "/AppData/Roaming/" + app_name;
    }

    // Fallback
    return home + "/." + app_name;
}

fn get_data_dir(app_name: string): string {
    let p = platform();
    let home = homedir();

    if (p == "linux") {
        return home + "/.local/share/" + app_name;
    } else if (p == "macos") {
        return home + "/Library/" + app_name;
    } else if (p == "windows") {
        return home + "/AppData/Local/" + app_name;
    }

    return home + "/." + app_name + "/data";
}

print("Config: " + get_config_dir("myapp"));
print("Data: " + get_data_dir("myapp"));
```

### System Monitoring

```hemlock
import * as os from "@stdlib/os";
import { sleep } from "@stdlib/time";

fn monitor_system(interval_seconds: i32, iterations: i32): null {
    print("=== System Monitor ===");
    print("Host: " + os.hostname());
    print("Platform: " + os.platform() + " " + os.arch());
    print("CPUs: " + typeof(os.cpu_count()));
    print("");

    let i = 0;
    while (i < iterations) {
        let mem = os.memory_info();
        let usage_percent = (mem.used * 100) / mem.total;

        print("[" + typeof(i + 1) + "] Memory: " +
              os.format_bytes(mem.used) + " / " +
              os.format_bytes(mem.total) +
              " (" + typeof(usage_percent) + "%)");

        sleep(interval_seconds);
        i = i + 1;
    }

    return null;
}

// Monitor every 5 seconds, 10 times
monitor_system(5, 10);
```

### Worker Pool Sizing

```hemlock
import { cpu_count, total_memory, format_bytes } from "@stdlib/os";

fn calculate_workers(memory_per_worker: i64): i32 {
    let cores = cpu_count();
    let total = total_memory();

    // Workers based on CPU (leave 1 core for system)
    let cpu_workers = cores - 1;
    if (cpu_workers < 1) {
        cpu_workers = 1;
    }

    // Workers based on memory (use 75% of total)
    let available = (total * 75) / 100;
    let mem_workers: i32 = available / memory_per_worker;
    if (mem_workers < 1) {
        mem_workers = 1;
    }

    // Use the lower of the two
    let workers = cpu_workers;
    if (mem_workers < workers) {
        workers = mem_workers;
    }

    print("Calculated workers:");
    print("  CPU-based: " + typeof(cpu_workers) + " (from " + typeof(cores) + " cores)");
    print("  Memory-based: " + typeof(mem_workers) + " (" + format_bytes(available) + " available)");
    print("  Using: " + typeof(workers) + " workers");

    return workers;
}

// Assume each worker needs 512 MB
let worker_memory: i64 = 536870912;  // 512 MB
let num_workers = calculate_workers(worker_memory);
```

### Platform-Specific Commands

```hemlock
import { platform } from "@stdlib/os";
import { exec } from "@stdlib/process";

fn open_url(url: string): null {
    let p = platform();
    let cmd = "";

    if (p == "linux") {
        cmd = "xdg-open " + url;
    } else if (p == "macos") {
        cmd = "open " + url;
    } else if (p == "windows") {
        cmd = "start " + url;
    } else {
        print("Cannot open URL on unknown platform");
        return null;
    }

    let result = exec(cmd);
    if (result.exit_code != 0) {
        print("Failed to open URL: " + result.output);
    }

    return null;
}

fn get_clipboard(): string {
    let p = platform();

    if (p == "linux") {
        return exec("xclip -selection clipboard -o").output;
    } else if (p == "macos") {
        return exec("pbpaste").output;
    }

    return "";
}

fn set_clipboard(text: string): null {
    let p = platform();

    if (p == "linux") {
        exec("echo -n '" + text + "' | xclip -selection clipboard");
    } else if (p == "macos") {
        exec("echo -n '" + text + "' | pbcopy");
    }

    return null;
}
```

---

## Platform Differences

### Memory Reporting

| Platform | total_memory() | free_memory() |
|----------|---------------|---------------|
| Linux | Uses `sysinfo()` | freeram + bufferram |
| macOS | Uses `sysctl(HW_MEMSIZE)` | Uses `sysconf(_SC_AVPHYS_PAGES)` |

**Note:** "Free memory" definitions vary by platform. On Linux, buffers and cache are included as "available" since they can be reclaimed.

### Uptime

| Platform | Implementation |
|----------|---------------|
| Linux | Uses `sysinfo()` |
| macOS | Uses `sysctl(KERN_BOOTTIME)` |

### Hostname

Uses standard `gethostname()` system call on all platforms.

---

## Testing

Run the os module tests:

```bash
# Run all os tests
make test | grep stdlib_os

# Or run individual test
./hemlock tests/stdlib_os/test_os.hml
```

---

## See Also

- **Environment module** (`@stdlib/env`) - Environment variables, process ID, exit
- **Process module** (`@stdlib/process`) - Process management, command execution
- **Filesystem module** (`@stdlib/fs`) - File and directory operations
- **Time module** (`@stdlib/time`) - Time functions

---

## License

Part of the Hemlock standard library.
