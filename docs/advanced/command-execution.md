# Command Execution in Hemlock

Hemlock provides the **`exec()` builtin function** to execute shell commands and capture their output.

## Table of Contents

- [Overview](#overview)
- [The exec() Function](#the-exec-function)
- [Result Object](#result-object)
- [Basic Usage](#basic-usage)
- [Advanced Examples](#advanced-examples)
- [Error Handling](#error-handling)
- [Implementation Details](#implementation-details)
- [Security Considerations](#security-considerations)
- [Limitations](#limitations)
- [Use Cases](#use-cases)
- [Best Practices](#best-practices)
- [Complete Examples](#complete-examples)

## Overview

The `exec()` function allows Hemlock programs to:
- Execute shell commands
- Capture standard output (stdout)
- Check exit status codes
- Use shell features (pipes, redirects, etc.)
- Integrate with system utilities

**Important:** Commands are executed via `/bin/sh`, giving full shell capabilities but also introducing security considerations.

## The exec() Function

### Signature

```hemlock
exec(command: string): object
```

**Parameters:**
- `command` (string) - Shell command to execute

**Returns:** An object with two fields:
- `output` (string) - The command's stdout output
- `exit_code` (i32) - The command's exit status code

### Basic Example

```hemlock
import { exec } from "@stdlib/process";

let result = exec("echo hello");
print(result.output);      // "hello\n"
print(result.exit_code);   // 0
```

## Result Object

The object returned by `exec()` has the following structure:

```hemlock
{
    output: string,      // Command stdout (captured output)
    exit_code: i32       // Process exit status (0 = success)
}
```

### output Field

Contains all text written to stdout by the command.

**Properties:**
- Empty string if command produces no output
- Includes newlines and whitespace as-is
- Multi-line output preserved
- Not limited in size (dynamically allocated)

**Examples:**
```hemlock
import { exec } from "@stdlib/process";

let r1 = exec("echo test");
print(r1.output);  // "test\n"

let r2 = exec("ls");
print(r2.output);  // Directory listing with newlines

let r3 = exec("true");
print(r3.output);  // "" (empty string)
```

### exit_code Field

The command's exit status code.

**Values:**
- `0` typically indicates success
- `1-255` indicate errors (convention varies by command)
- `-1` if command could not be executed or terminated abnormally

**Examples:**
```hemlock
let r1 = exec("true");
print(r1.exit_code);  // 0 (success)

let r2 = exec("false");
print(r2.exit_code);  // 1 (failure)

let r3 = exec("ls /nonexistent");
print(r3.exit_code);  // 2 (file not found, varies by command)
```

## Basic Usage

### Simple Command

```hemlock
import { exec } from "@stdlib/process";

let r = exec("ls -la");
print(r.output);
print("Exit code: " + typeof(r.exit_code));
```

### Checking Exit Status

```hemlock
import { exec } from "@stdlib/process";

let r = exec("grep pattern file.txt");
if (r.exit_code == 0) {
    print("Found: " + r.output);
} else {
    print("Pattern not found");
}
```

### Commands with Pipes

```hemlock
let r = exec("ps aux | grep hemlock");
print(r.output);
```

### Multiple Commands

```hemlock
let r = exec("cd /tmp && ls -la");
print(r.output);
```

### Command Substitution

```hemlock
let r = exec("echo $(date)");
print(r.output);  // Current date
```

## Advanced Examples

### Handling Failures

```hemlock
let r = exec("ls /nonexistent");
if (r.exit_code != 0) {
    print("Command failed with code: " + typeof(r.exit_code));
    print("Error output: " + r.output);  // Note: stderr not captured
}
```

### Processing Multi-Line Output

```hemlock
import { exec } from "@stdlib/process";

let r = exec("cat file.txt");
let lines = r.output.split("\n");
let i = 0;
while (i < lines.length) {
    print("Line " + typeof(i) + ": " + lines[i]);
    i = i + 1;
}
```

### Command Chaining

**With && (AND):**
```hemlock
let r1 = exec("mkdir -p /tmp/test && touch /tmp/test/file.txt");
if (r1.exit_code == 0) {
    print("Setup complete");
}
```

**With || (OR):**
```hemlock
let r = exec("command1 || command2");
// Runs command2 only if command1 fails
```

**With ; (sequence):**
```hemlock
let r = exec("command1; command2");
// Runs both regardless of success/failure
```

### Using Pipes

```hemlock
let r = exec("echo 'data' | base64");
print("Base64: " + r.output);
```

**Complex pipelines:**
```hemlock
let r = exec("cat /etc/passwd | grep root | cut -d: -f1");
print(r.output);
```

### Exit Code Patterns

Different exit codes indicate different conditions:

```hemlock
let r = exec("test -f myfile.txt");
if (r.exit_code == 0) {
    print("File exists");
} else if (r.exit_code == 1) {
    print("File does not exist");
} else {
    print("Test command failed: " + typeof(r.exit_code));
}
```

### Output Redirects

```hemlock
// Redirect stdout to file (within shell)
let r1 = exec("echo 'test' > /tmp/output.txt");

// Redirect stderr to stdout (Note: stderr still not captured by Hemlock)
let r2 = exec("command 2>&1");
```

### Environment Variables

```hemlock
let r = exec("export VAR=value && echo $VAR");
print(r.output);  // "value\n"
```

### Working Directory Changes

```hemlock
let r = exec("cd /tmp && pwd");
print(r.output);  // "/tmp\n"
```

## Error Handling

### When exec() Throws Exceptions

The `exec()` function throws an exception if the command cannot be executed:

```hemlock
try {
    let r = exec("nonexistent_command_xyz");
} catch (e) {
    print("Failed to execute: " + e);
}
```

**Exceptions are thrown when:**
- `popen()` fails (e.g., cannot create pipe)
- System resource limits exceeded
- Memory allocation failures

### When exec() Does NOT Throw

```hemlock
// Command runs but returns non-zero exit code
let r1 = exec("false");
print(r1.exit_code);  // 1 (not an exception)

// Command produces no output
let r2 = exec("true");
print(r2.output);  // "" (not an exception)

// Command not found by shell
let r3 = exec("nonexistent_cmd");
print(r3.exit_code);  // 127 (not an exception)
```

### Safe Execution Pattern

```hemlock
import { exec } from "@stdlib/process";

fn safe_exec(command: string) {
    try {
        let r = exec(command);
        if (r.exit_code != 0) {
            print("Warning: Command failed with code " + typeof(r.exit_code));
            return "";
        }
        return r.output;
    } catch (e) {
        print("Error executing command: " + e);
        return "";
    }
}

let output = safe_exec("ls -la");
```

## Implementation Details

### How It Works

**Under the hood:**
- Uses `popen()` to execute commands via `/bin/sh`
- Captures stdout only (stderr is not captured)
- Output buffered dynamically (starts at 4KB, grows as needed)
- Exit status extracted using `WIFEXITED()` and `WEXITSTATUS()` macros
- Output string is properly null-terminated

**Process flow:**
1. `popen(command, "r")` creates pipe and forks process
2. Child process executes `/bin/sh -c "command"`
3. Parent reads stdout via pipe into growing buffer
4. `pclose()` waits for child and returns exit status
5. Exit status is extracted and stored in result object

### Performance Considerations

**Costs:**
- Creates a new shell process for each call (~1-5ms overhead)
- Output stored entirely in memory (not streamed)
- No streaming support (waits for command completion)
- Suitable for commands with reasonable output sizes

**Optimizations:**
- Buffer starts at 4KB and doubles when full (efficient memory usage)
- Single read loop minimizes system calls
- No additional string copying

**When to use:**
- Short-running commands (< 1 second)
- Moderate output size (< 10MB)
- Batch operations with reasonable intervals

**When NOT to use:**
- Long-running daemons or services
- Commands producing gigabytes of output
- Real-time streaming data processing
- High-frequency execution (> 100 calls/second)

## Security Considerations

### Shell Injection Risk

⚠️ **CRITICAL:** Commands are executed by the shell (`/bin/sh`), which means **shell injection is possible**.

**Vulnerable code:**
```hemlock
// DANGEROUS - DO NOT DO THIS
let filename = args[1];  // User input
let r = exec("cat " + filename);  // Shell injection!
```

**Attack:**
```bash
./hemlock script.hml "; rm -rf /; echo pwned"
# Executes: cat ; rm -rf /; echo pwned
```

### Safe Practices

**1. Never use unsanitized user input:**
```hemlock
// Bad
let user_input = args[1];
let r = exec("process " + user_input);  // DANGEROUS

// Good - validate first
fn is_safe_filename(name: string): bool {
    // Only allow alphanumeric, dash, underscore, dot
    let i = 0;
    while (i < name.length) {
        let c = name[i];
        if (!(c >= 'a' && c <= 'z') &&
            !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') &&
            c != '-' && c != '_' && c != '.') {
            return false;
        }
        i = i + 1;
    }
    return true;
}

let filename = args[1];
if (is_safe_filename(filename)) {
    let r = exec("cat " + filename);
} else {
    print("Invalid filename");
}
```

**2. Use allowlists, not denylists:**
```hemlock
// Good - strict allowlist
let allowed_commands = ["status", "start", "stop", "restart"];
let cmd = args[1];

let found = false;
for (let allowed in allowed_commands) {
    if (cmd == allowed) {
        found = true;
        break;
    }
}

if (found) {
    exec("service myapp " + cmd);
} else {
    print("Invalid command");
}
```

**3. Escape special characters:**
```hemlock
fn shell_escape(s: string): string {
    // Simple escape - wrap in single quotes and escape single quotes
    let escaped = s.replace_all("'", "'\\''");
    return "'" + escaped + "'";
}

let user_file = args[1];
let safe = shell_escape(user_file);
let r = exec("cat " + safe);
```

**4. Avoid exec() for file operations:**
```hemlock
// Bad - use exec for file operations
let r = exec("cat file.txt");

// Good - use Hemlock's file API
let f = open("file.txt", "r");
let content = f.read();
f.close();
```

### Permission Considerations

Commands run with the same permissions as the Hemlock process:

```hemlock
// If Hemlock runs as root, exec() commands also run as root!
let r = exec("rm -rf /important");  // DANGEROUS if running as root
```

**Best practice:** Run Hemlock with least privilege necessary.

## Limitations

### 1. No stderr Capture

Only stdout is captured, stderr goes to terminal:

```hemlock
let r = exec("ls /nonexistent");
// r.output is empty
// Error message appears on terminal, not captured
```

**Workaround - redirect stderr to stdout:**
```hemlock
let r = exec("ls /nonexistent 2>&1");
// Now error messages are in r.output
```

### 2. No Streaming

Must wait for command completion:

```hemlock
let r = exec("long_running_command");
// Blocks until command finishes
// Cannot process output incrementally
```

### 3. No Timeout

Commands can run indefinitely:

```hemlock
let r = exec("sleep 1000");
// Blocks for 1000 seconds
// No way to timeout or cancel
```

**Workaround - use timeout command:**
```hemlock
let r = exec("timeout 5 long_command");
// Will timeout after 5 seconds
```

### 4. No Signal Handling

Cannot send signals to running commands:

```hemlock
let r = exec("long_command");
// Cannot send SIGINT, SIGTERM, etc. to the command
```

### 5. No Process Control

Cannot interact with command after starting:

```hemlock
let r = exec("interactive_program");
// Cannot send input to the program
// Cannot control execution
```

## Use Cases

### Good Use Cases

**1. Running system utilities:**
```hemlock
let r = exec("ls -la");
let r = exec("grep pattern file.txt");
let r = exec("find /path -name '*.txt'");
```

**2. Quick data processing with Unix tools:**
```hemlock
let r = exec("cat data.txt | sort | uniq | wc -l");
print("Unique lines: " + r.output);
```

**3. Checking system state:**
```hemlock
let r = exec("df -h");
print("Disk usage:\n" + r.output);
```

**4. File existence checks:**
```hemlock
let r = exec("test -f myfile.txt");
if (r.exit_code == 0) {
    print("File exists");
}
```

**5. Generating reports:**
```hemlock
let r = exec("ps aux | grep myapp | wc -l");
let count = r.output.trim();
print("Running instances: " + count);
```

**6. Automation scripts:**
```hemlock
exec("git add .");
exec("git commit -m 'Auto commit'");
let r = exec("git push");
if (r.exit_code != 0) {
    print("Push failed");
}
```

### Not Recommended For

**1. Long-running services:**
```hemlock
// Bad
let r = exec("nginx");  // Blocks forever
```

**2. Interactive commands:**
```hemlock
// Bad - cannot provide input
let r = exec("ssh user@host");
```

**3. Commands producing huge output:**
```hemlock
// Bad - loads entire output into memory
let r = exec("cat 10GB_file.log");
```

**4. Real-time streaming:**
```hemlock
// Bad - cannot process output incrementally
let r = exec("tail -f /var/log/app.log");
```

**5. Mission-critical error handling:**
```hemlock
// Bad - stderr not captured
let r = exec("critical_operation");
// Cannot see detailed error messages
```

## Best Practices

### 1. Always Check Exit Codes

```hemlock
let r = exec("important_command");
if (r.exit_code != 0) {
    print("Command failed!");
    // Handle error
}
```

### 2. Trim Output When Needed

```hemlock
let r = exec("echo test");
let clean = r.output.trim();  // Remove trailing newline
print(clean);  // "test" (no newline)
```

### 3. Validate Before Executing

```hemlock
fn is_valid_command(cmd: string): bool {
    // Validate command is safe
    return true;  // Your validation logic
}

if (is_valid_command(user_cmd)) {
    exec(user_cmd);
}
```

### 4. Use try/catch for Critical Operations

```hemlock
try {
    let r = exec("critical_command");
    if (r.exit_code != 0) {
        throw "Command failed";
    }
} catch (e) {
    print("Error: " + e);
    // Cleanup or recovery
}
```

### 5. Prefer Hemlock APIs Over exec()

```hemlock
// Bad - use exec for file operations
let r = exec("cat file.txt");

// Good - use Hemlock's File API
let f = open("file.txt", "r");
let content = f.read();
f.close();
```

### 6. Capture stderr When Needed

```hemlock
// Redirect stderr to stdout
let r = exec("command 2>&1");
// Now r.output contains both stdout and stderr
```

### 7. Use Shell Features Wisely

```hemlock
// Use pipes for efficiency
let r = exec("cat large.txt | grep pattern | head -n 10");

// Use command substitution
let r = exec("echo Current user: $(whoami)");

// Use conditional execution
let r = exec("test -f file.txt && cat file.txt");
```

## Complete Examples

### Example 1: System Information Gatherer

```hemlock
import { exec } from "@stdlib/process";

fn get_system_info() {
    print("=== System Information ===");

    // Hostname
    let r1 = exec("hostname");
    print("Hostname: " + r1.output.trim());

    // Uptime
    let r2 = exec("uptime");
    print("Uptime: " + r2.output.trim());

    // Disk usage
    let r3 = exec("df -h /");
    print("\nDisk Usage:");
    print(r3.output);

    // Memory usage
    let r4 = exec("free -h");
    print("Memory Usage:");
    print(r4.output);
}

get_system_info();
```

### Example 2: Log Analyzer

```hemlock
import { exec } from "@stdlib/process";

fn analyze_log(logfile: string) {
    print("Analyzing log: " + logfile);

    // Count total lines
    let r1 = exec("wc -l " + logfile);
    print("Total lines: " + r1.output.trim());

    // Count errors
    let r2 = exec("grep -c ERROR " + logfile + " 2>/dev/null");
    let errors = r2.output.trim();
    if (r2.exit_code == 0) {
        print("Errors: " + errors);
    } else {
        print("Errors: 0");
    }

    // Count warnings
    let r3 = exec("grep -c WARN " + logfile + " 2>/dev/null");
    let warnings = r3.output.trim();
    if (r3.exit_code == 0) {
        print("Warnings: " + warnings);
    } else {
        print("Warnings: 0");
    }

    // Recent errors
    print("\nRecent errors:");
    let r4 = exec("grep ERROR " + logfile + " | tail -n 5");
    print(r4.output);
}

if (args.length < 2) {
    print("Usage: " + args[0] + " <logfile>");
} else {
    analyze_log(args[1]);
}
```

### Example 3: Git Helper

```hemlock
import { exec } from "@stdlib/process";

fn git_status() {
    let r = exec("git status --short");
    if (r.exit_code != 0) {
        print("Error: Not a git repository");
        return;
    }

    if (r.output == "") {
        print("Working directory clean");
    } else {
        print("Changes:");
        print(r.output);
    }
}

fn git_quick_commit(message: string) {
    print("Adding all changes...");
    let r1 = exec("git add -A");
    if (r1.exit_code != 0) {
        print("Error adding files");
        return;
    }

    print("Committing...");
    let safe_msg = message.replace_all("'", "'\\''");
    let r2 = exec("git commit -m '" + safe_msg + "'");
    if (r2.exit_code != 0) {
        print("Error committing");
        return;
    }

    print("Committed successfully");
    print(r2.output);
}

// Usage
git_status();
if (args.length > 1) {
    git_quick_commit(args[1]);
}
```

### Example 4: Backup Script

```hemlock
import { exec } from "@stdlib/process";

fn backup_directory(source: string, dest: string) {
    print("Backing up " + source + " to " + dest);

    // Create backup directory
    let r1 = exec("mkdir -p " + dest);
    if (r1.exit_code != 0) {
        print("Error creating backup directory");
        return false;
    }

    // Create tarball with timestamp
    let r2 = exec("date +%Y%m%d_%H%M%S");
    let timestamp = r2.output.trim();
    let backup_file = dest + "/backup_" + timestamp + ".tar.gz";

    print("Creating archive: " + backup_file);
    let r3 = exec("tar -czf " + backup_file + " " + source + " 2>&1");
    if (r3.exit_code != 0) {
        print("Error creating backup:");
        print(r3.output);
        return false;
    }

    print("Backup completed successfully");

    // Show backup size
    let r4 = exec("du -h " + backup_file);
    print("Backup size: " + r4.output.trim());

    return true;
}

if (args.length < 3) {
    print("Usage: " + args[0] + " <source> <destination>");
} else {
    backup_directory(args[1], args[2]);
}
```

## Summary

Hemlock's `exec()` function provides:

- ✅ Simple shell command execution
- ✅ Output capture (stdout)
- ✅ Exit code checking
- ✅ Full shell feature access (pipes, redirects, etc.)
- ✅ Integration with system utilities

Remember:
- Always check exit codes
- Be aware of security implications (shell injection)
- Validate user input before using in commands
- Prefer Hemlock APIs over exec() when available
- stderr is not captured (use `2>&1` to redirect)
- Commands block until completion
- Use for short-running utilities, not long-running services

**Security checklist:**
- ❌ Never use unsanitized user input
- ✅ Validate all input
- ✅ Use allowlists for commands
- ✅ Escape special characters when necessary
- ✅ Run with least privilege
- ✅ Prefer Hemlock APIs over shell commands
