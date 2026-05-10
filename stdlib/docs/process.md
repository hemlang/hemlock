# @stdlib/process - Process Management

Comprehensive process control and inter-process communication for Hemlock.

## Overview

The `@stdlib/process` module provides POSIX-compliant process management capabilities including process identification, control, creation, and command execution.

## Import

```hemlock
// Import specific functions
import { get_pid, getppid, exec, kill } from "@stdlib/process";

// Import all as namespace
import * as process from "@stdlib/process";
```

## Process Identification

### `get_pid(): i32`

Get the current process ID.

```hemlock
let pid = get_pid();
print("Current PID: " + typeof(pid));
```

### `getppid(): i32`

Get the parent process ID.

```hemlock
let ppid = getppid();
print("Parent PID: " + typeof(ppid));
```

### `getuid(): i32`

Get the real user ID of the current process.

```hemlock
let uid = getuid();
print("User ID: " + typeof(uid));
```

### `geteuid(): i32`

Get the effective user ID of the current process.

```hemlock
let euid = geteuid();
```

### `getgid(): i32`

Get the real group ID of the current process.

```hemlock
let gid = getgid();
print("Group ID: " + typeof(gid));
```

### `getegid(): i32`

Get the effective group ID of the current process.

```hemlock
let egid = getegid();
```

## Process Control

### `exit(code?: i32)`

Terminate the current process with an optional exit code (default: 0).

```hemlock
// Exit with success
exit();

// Exit with error code
if (error_occurred) {
    exit(1);
}
```

**Note:** This function never returns.

### `kill(pid: i32, signal: i32)`

Send a signal to a process.

```hemlock
// Send SIGTERM (15) to process
kill(target_pid, 15);

// Send signal 0 to check if process exists
try {
    kill(target_pid, 0);
    print("Process exists");
} catch (e) {
    print("Process does not exist");
}
```

**Common signals:**
- `1` - SIGHUP (Hangup)
- `2` - SIGINT (Interrupt, Ctrl+C)
- `9` - SIGKILL (Kill, cannot be caught)
- `15` - SIGTERM (Termination)

**Exceptions:** Throws on error (e.g., permission denied, process not found).

### `abort()`

Abort the current process, generating a core dump.

```hemlock
if (critical_error) {
    abort();  // Terminates immediately with core dump
}
```

**Note:** This function never returns.

## Process Creation

### `fork(): i32`

Fork the current process, creating a child process.

**Returns:**
- `0` in the child process
- Child's PID in the parent process

```hemlock
let pid = fork();

if (pid == 0) {
    // Child process
    print("I'm the child!");
    exit(0);
} else {
    // Parent process
    print("Child PID: " + typeof(pid));
    let result = waitpid(pid, 0);
}
```

**⚠️ Warning:** Use `fork()` with caution in the interpreter. The child process inherits all interpreter state which can cause issues. Prefer using `exec()` for running external commands.

### `wait(): object`

Wait for any child process to change state.

**Returns:** Object with fields:
- `pid` (i32) - Process ID of the child that changed state
- `status` (i32) - Exit status of the child

```hemlock
let result = wait();
print("Child " + typeof(result.pid) + " exited with status: " + typeof(result.status));
```

**Exceptions:** Throws if no child processes exist or wait() fails.

### `waitpid(pid: i32, options: i32): object`

Wait for a specific child process to change state.

**Parameters:**
- `pid` - Process ID to wait for
- `options` - Wait options (see Wait Options below)

**Returns:** Same as `wait()`

```hemlock
let child_pid = fork();

if (child_pid == 0) {
    // Child work
    exit(0);
} else {
    // Wait for specific child
    let result = waitpid(child_pid, 0);
    print("Child exited: " + typeof(result.status));
}
```

**Wait Options:**
- `0` - Block until child exits
- `1` (WNOHANG) - Return immediately if no child has exited
- `2` (WUNTRACED) - Also report stopped children

## Command Execution

### `exec(command: string): object`

Execute a shell command and capture its output.

**Returns:** Object with fields:
- `output` (string) - Command's stdout
- `exit_code` (i32) - Command's exit status

```hemlock
let result = exec("echo 'Hello World'");
print(result.output);        // "Hello World\n"
print(result.exit_code);     // 0

// Check exit code
let result2 = exec("grep pattern file.txt");
if (result2.exit_code == 0) {
    print("Found: " + result2.output);
} else {
    print("Pattern not found");
}
```

**Notes:**
- Commands are executed via `/bin/sh`
- Only stdout is captured (stderr goes to terminal)
- **⚠️ Shell injection risk:** Always sanitize user input before passing to `exec()`

**Exceptions:** Throws if command cannot be executed.

### `exec_argv(argv: array<string>): object`

Execute a command without shell interpretation. This is the safe alternative to `exec()` that prevents shell injection attacks and properly captures both stdout and stderr.

**Parameters:**
- `argv` - Array of strings: `["command", "arg1", "arg2", ...]`

**Returns:** Object with fields:
- `output` (string) - Command's stdout
- `stderr` (string) - Command's stderr (properly captured!)
- `exit_code` (i32) - Command's exit status

```hemlock
// Basic usage
let result = exec_argv(["ls", "-la", "/tmp"]);
print(result.output);      // stdout
print(result.stderr);      // stderr (empty for ls)
print(result.exit_code);   // 0

// Command with stderr output
let result2 = exec_argv(["sh", "-c", "echo out; echo err >&2"]);
print(result2.output);     // "out\n"
print(result2.stderr);     // "err\n"

// Shell injection is prevented
let user_input = "file.txt; rm -rf /";
let result3 = exec_argv(["cat", user_input]);
// This safely tries to cat a file literally named "file.txt; rm -rf /"
// No shell interpretation occurs
```

**Key differences from `exec()`:**
| Feature | `exec()` | `exec_argv()` |
|---------|----------|---------------|
| Shell interpretation | Yes | No |
| Stderr capture | No (goes to terminal) | Yes (in `stderr` field) |
| Shell injection risk | Yes | No |
| Pipes/redirects | Supported | Not supported (use array elements) |
| Glob expansion | Supported | Not supported |

**When to use `exec_argv()`:**
- User-provided arguments (prevents injection)
- Need to capture stderr separately
- Running commands with arguments containing special characters

**Exceptions:** Throws if command cannot be found or executed.

### `posix_spawn(argv: array<string>, opts?: object): object`

Detached process spawn backed by `posix_spawn(3)`. Unlike `exec_argv()` (which forks, captures stdout/stderr through pipes, and waits for the child), `posix_spawn()` returns immediately after the child is created. The caller owns the pid and is responsible for reaping it via `waitpid()` or arranging `SIGCHLD` handling.

The export is named `posix_spawn` (not `spawn`) to avoid colliding with the language-level task `spawn(fn, args...)` builtin in the compiler backend.

**Parameters:**
- `argv` - Array of strings; `argv[0]` is the program name. PATH is searched unless the name contains `/`.
- `opts` (optional object):
  - `env`: array of `"KEY=value"` strings (default: inherit parent environ)
  - `stdin`: i32 fd to dup2 onto `STDIN_FILENO` in child (default: inherit)
  - `stdout`: i32 fd to dup2 onto `STDOUT_FILENO` in child (default: inherit)
  - `stderr`: i32 fd to dup2 onto `STDERR_FILENO` in child (default: inherit)
  - `cwd`: string; chdir before exec. Requires glibc 2.29+ / macOS 10.15+; older systems throw at runtime.
  - `setsid`: bool; child becomes a session leader, detaching from the controlling terminal. Requires `POSIX_SPAWN_SETSID`.

**Returns:** Object with field:
- `pid` (i32) - Child process id

```hemlock
import { posix_spawn, waitpid } from "@stdlib/process";
import { open_fd } from "@stdlib/fs";

// Fire-and-forget
let { pid } = posix_spawn(["./worker", "--once"]);

// Redirect stdout/stderr into a log file (no `sh -c` wrapper needed)
let log_fd = open_fd("/tmp/worker.log", "a");
let child = posix_spawn(
    ["./worker"],
    { stdout: log_fd, stderr: log_fd }
);

// Daemonize: run in a new session, no controlling terminal
let daemon = posix_spawn(
    ["./long-running"],
    { setsid: true, cwd: "/" }
);

// Reap when the parent is ready
let status = waitpid(pid, 0);
```

**Comparison with other process primitives:**
| Primitive | Behavior | Stdio | Use when |
|-----------|----------|-------|----------|
| `fork()` | Manual fork, racy under threads | Inherited | Need control over both child and parent halves |
| `exec()` | Shell-interpreted, blocks until exit | Captures stdout only | Quick shell one-liners |
| `exec_argv()` | Forks, blocks until exit | Captures stdout + stderr | Capturing output of a finite command |
| `posix_spawn()` | Returns pid immediately, caller reaps | Inherited or fd-redirected | "Launch and walk away" |

**Platform notes:**
- WASM builds throw at call time — emscripten libc has no `posix_spawn(3)`.
- `cwd` and `setsid` are gated behind their respective glibc / macOS / `POSIX_SPAWN_SETSID` checks; on older systems calling with these options throws a clear runtime error rather than silently ignoring them.

**Exceptions:** Throws if `argv` is empty, the executable cannot be found, or an option requires a feature missing on the host libc.

## Usage Patterns

### Check if process exists

```hemlock
fn process_exists(pid: i32) {
    try {
        kill(pid, 0);  // Signal 0 doesn't actually send a signal
        return true;
    } catch (e) {
        return false;
    }
}

if (process_exists(1234)) {
    print("Process 1234 is running");
}
```

### Execute and check success

```hemlock
fn exec_success(command: string) {
    let result = exec(command);
    return result.exit_code == 0;
}

if (exec_success("test -f myfile.txt")) {
    print("File exists");
}
```

### Get all process info

```hemlock
fn get_process_info() {
    let info = {};
    info.pid = get_pid();
    info.ppid = getppid();
    info.uid = getuid();
    info.euid = geteuid();
    info.gid = getgid();
    info.egid = getegid();
    return info;
}

let info = get_process_info();
print("PID: " + typeof(info.pid));
print("UID: " + typeof(info.uid));
```

### Graceful process termination

```hemlock
fn terminate_gracefully(pid: i32) {
    // Try SIGTERM first
    try {
        kill(pid, 15);  // SIGTERM
        let i = 0;
        while (i < 10) {
            if (!process_exists(pid)) {
                return true;
            }
            import { sleep } from "@stdlib/time";
            sleep(0.1);
            i = i + 1;
        }
        // Force kill if still running
        kill(pid, 9);  // SIGKILL
        return true;
    } catch (e) {
        return false;
    }
}
```

## Platform Notes

- **POSIX compliant:** All functions follow POSIX standards
- **Signal numbers:** May vary by platform (use standard values documented above)
- **fork() limitations:** Not recommended in the interpreter due to state copying issues

## Security Considerations

### Command Injection

The `exec()` function executes commands via the shell, which can be dangerous with untrusted input:

```hemlock
// ❌ UNSAFE - vulnerable to command injection
let user_input = "file.txt; rm -rf /";
let result = exec("cat " + user_input);

// ✅ SAFE - validate/sanitize input
fn safe_exec(filename: string) {
    // Validate filename contains only safe characters
    let i = 0;
    while (i < filename.length) {
        let ch = filename[i];
        if (!(ch >= 'a' && ch <= 'z') &&
            !(ch >= 'A' && ch <= 'Z') &&
            !(ch >= '0' && ch <= '9') &&
            ch != '.' && ch != '_' && ch != '-') {
            throw "Invalid filename";
        }
        i = i + 1;
    }
    return exec("cat " + filename);
}
```

### Permission Checks

Always check if operations succeed:

```hemlock
try {
    kill(target_pid, 15);
    print("Signal sent successfully");
} catch (e) {
    print("Failed to send signal: " + e);
}
```

## Error Handling

All functions that can fail throw exceptions:

```hemlock
try {
    let result = exec("nonexistent_command");
} catch (e) {
    print("Execution failed: " + e);
}

try {
    kill(99999, 15);  // Non-existent PID
} catch (e) {
    print("Could not signal process: " + e);
}
```

## Complete Example

```hemlock
import { get_pid, getppid, exec, kill } from "@stdlib/process";
import { sleep } from "@stdlib/time";

// Display process info
print("=== Process Info ===");
print("PID: " + typeof(get_pid()));
print("PPID: " + typeof(getppid()));

// Execute commands
print("\n=== Command Execution ===");
let result = exec("uname -s");
print("OS: " + result.output.trim());

let files = exec("ls -1 | wc -l");
print("Files in directory: " + files.output.trim());

// Check process status
print("\n=== Process Management ===");
fn check_process(name: string) {
    let result = exec("pgrep " + name);
    if (result.exit_code == 0) {
        print(name + " is running (PID: " + result.output.trim() + ")");
        return true;
    } else {
        print(name + " is not running");
        return false;
    }
}

check_process("ssh");
check_process("httpd");
```

## See Also

- **@stdlib/env** - Environment variables and process environment
- **@stdlib/time** - Time and sleep functions
- **@stdlib/fs** - File system operations
