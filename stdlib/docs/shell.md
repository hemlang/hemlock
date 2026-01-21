# @stdlib/shell - Shell Utilities

Provides utilities for building shell commands safely, escaping arguments, parsing output, and executing commands.

## Import

```hemlock
import { escape, quote, run, run_capture } from "@stdlib/shell";
import { which, command_exists, env_or } from "@stdlib/shell";
import { build_command, pipe, and_then } from "@stdlib/shell";
```

## Argument Escaping

### escape(s: string): string

Escape shell metacharacters with backslashes.

```hemlock
escape("hello world");   // "hello\ world"
escape("$HOME");         // "\$HOME"
escape("test*.txt");     // "test\*.txt"
escape("a & b; c");      // "a\ \&\ b\;\ c"
```

### quote(s: string): string

Quote a string with single quotes (safest method).

```hemlock
quote("hello world");    // "'hello world'"
quote("it's fine");      // "'it'\"'\"'s fine'"
quote("$HOME");          // "'$HOME'" (no expansion)
```

### double_quote(s: string): string

Quote with double quotes (allows variable expansion in shell).

```hemlock
double_quote("$HOME");   // "\"\\$HOME\""
double_quote('say "hi"'); // "\"say \\\"hi\\\"\""
```

## Command Building

### build_command(parts: array): string

Build a command string from parts, quoting each argument.

```hemlock
build_command(["ls", "-la", "/tmp"]);
// "'ls' '-la' '/tmp'"

build_command(["grep", "hello world", "file.txt"]);
// "'grep' 'hello world' 'file.txt'"
```

### Command Chaining

```hemlock
// Run second only if first succeeds
and_then(["cmd1", "cmd2"]);  // "cmd1 && cmd2"

// Run second only if first fails
or_else(["cmd1", "cmd2"]);   // "cmd1 || cmd2"

// Run all regardless of success
sequential(["cmd1", "cmd2", "cmd3"]);  // "cmd1; cmd2; cmd3"

// Pipe output
pipe(["cat file", "grep pattern", "wc -l"]);
// "cat file | grep pattern | wc -l"
```

## Command Execution

### run(command): bool

Run a command and return success status.

```hemlock
if (run("make build")) {
    print("Build succeeded!");
}

// With array (auto-quoted)
run(["cp", "source file.txt", "dest/"]);
```

### run_capture(command): object

Run and capture output.

```hemlock
let result = run_capture("git status");
if (result["success"]) {
    print(result["stdout"]);
} else {
    print("Error: " + result["stderr"]);
}

// Result object:
// {
//   success: bool,
//   stdout: string,
//   stderr: string,
//   code: i32
// }
```

### run_output(command): string

Run and return stdout, throw on failure.

```hemlock
let version = run_output("python --version");
print(version);  // "Python 3.9.7"
```

### run_lines(command): array

Run and return stdout as array of lines.

```hemlock
let files = run_lines("ls -1 /tmp");
let i = 0;
while (i < files.length) {
    print("File: " + files[i]);
    i = i + 1;
}
```

## Environment

### env_or(name, default): string

Get environment variable or default value.

```hemlock
let home = env_or("HOME", "/tmp");
let editor = env_or("EDITOR", "nano");
```

### has_env(name): bool

Check if environment variable is set and non-empty.

```hemlock
if (has_env("DEBUG")) {
    print("Debug mode enabled");
}
```

### set_envs(vars: object)

Set multiple environment variables.

```hemlock
set_envs({
    PATH: "/usr/bin:/bin",
    HOME: "/home/user"
});
```

## Path Utilities

### which(command): string or null

Find command path, or null if not found.

```hemlock
let python = which("python3");
if (python != null) {
    print("Python at: " + python);
}
```

### command_exists(command): bool

Check if command exists in PATH.

```hemlock
if (command_exists("docker")) {
    print("Docker is available");
}
```

## File Operations

```hemlock
file_exists("/etc/passwd");  // true
dir_exists("/tmp");          // true

mkdir("/path/to/dir");       // Create directory (with parents)
rm("/tmp/file.txt");         // Remove file
rm("/tmp/dir", true);        // Remove directory recursively

cp("src.txt", "dst.txt");           // Copy file
cp("/src/dir", "/dst/dir", true);   // Copy directory recursively

mv("old.txt", "new.txt");    // Move/rename
```

## Output Parsing

### parse_columns(line): array

Parse whitespace-separated columns.

```hemlock
let cols = parse_columns("  col1   col2    col3  ");
// ["col1", "col2", "col3"]
```

### parse_env_output(output): object

Parse KEY=VALUE output.

```hemlock
let env = parse_env_output("FOO=bar\nBAZ=qux");
print(env["FOO"]);  // "bar"
print(env["BAZ"]);  // "qux"
```

### parse_table(output): array

Parse tabular output with header row.

```hemlock
let output = "NAME   SIZE   DATE\nfile1  100    2024-01-01\nfile2  200    2024-01-02";
let rows = parse_table(output);
// [
//   { NAME: "file1", SIZE: "100", DATE: "2024-01-01" },
//   { NAME: "file2", SIZE: "200", DATE: "2024-01-02" }
// ]
```

## Redirection

```hemlock
redirect_stdout("cmd", "out.txt");   // "cmd > 'out.txt'"
redirect_stderr("cmd", "err.txt");   // "cmd 2> 'err.txt'"
redirect_all("cmd", "log.txt");      // "cmd > 'log.txt' 2>&1"
```

## Background Execution

```hemlock
background("long_task");             // "long_task &"
subshell("cd /tmp && ls");           // "(cd /tmp && ls)"
nohup("daemon", "/var/log/out.log"); // "nohup daemon > '/var/log/out.log' 2>&1 &"
```

## Examples

### Safe Command Execution

```hemlock
import { build_command, run, run_output } from "@stdlib/shell";

fn backup_file(source: string, dest: string): bool {
    let cmd = build_command(["cp", "-p", source, dest]);
    return run(cmd);
}

// Safe even with spaces or special characters
backup_file("/path/to/my file.txt", "/backup/my file.txt");
```

### Script Automation

```hemlock
import { run, run_capture, command_exists, and_then } from "@stdlib/shell";

fn deploy() {
    // Check prerequisites
    if (!command_exists("docker")) {
        throw "Docker not installed";
    }
    if (!command_exists("kubectl")) {
        throw "kubectl not installed";
    }

    // Build and push
    let result = run_capture(and_then([
        "docker build -t myapp:latest .",
        "docker push myapp:latest",
        "kubectl apply -f k8s/"
    ]));

    if (!result["success"]) {
        print("Deploy failed: " + result["stderr"]);
        return false;
    }

    return true;
}
```

### Parsing Command Output

```hemlock
import { run_lines, parse_columns } from "@stdlib/shell";

fn list_processes() {
    let lines = run_lines("ps aux");
    let i = 1;  // Skip header
    while (i < lines.length) {
        let cols = parse_columns(lines[i]);
        if (cols.length >= 11) {
            print("PID: " + cols[1] + ", CMD: " + cols[10]);
        }
        i = i + 1;
    }
}
```

### Environment Setup

```hemlock
import { env_or, set_envs, has_env } from "@stdlib/shell";

fn setup_environment() {
    // Get with defaults
    let node_env = env_or("NODE_ENV", "development");
    let port = env_or("PORT", "3000");

    // Set multiple
    set_envs({
        APP_ENV: node_env,
        APP_PORT: port,
        APP_DEBUG: has_env("DEBUG") ? "1" : "0"
    });
}
```

## Safe Execution (No Shell Injection)

The `*_safe` functions use `exec_argv` internally, which bypasses shell interpretation entirely. This prevents shell injection attacks and properly captures both stdout and stderr separately.

### exec_safe(argv: array): object

Execute command without shell interpretation. Returns full result object.

```hemlock
let result = exec_safe(["ls", "-la", "/tmp"]);
print(result.output);     // stdout
print(result.stderr);     // stderr (properly captured!)
print(result.exit_code);  // 0 on success
```

### run_capture_safe(argv: array): object

Run command safely and return structured result with proper stderr capture.

```hemlock
let result = run_capture_safe(["git", "commit", "-m", "message"]);
// Returns: { success: bool, stdout: string, stderr: string, code: i32 }

if (!result.success) {
    print("Git error: " + result.stderr);
}
```

### run_output_safe(argv: array): string

Run command safely and return stdout, or throw on failure.

```hemlock
let version = run_output_safe(["python3", "--version"]);
print(version);  // "Python 3.9.7\n"
```

### run_safe(argv: array): bool

Run command safely and return success status.

```hemlock
if (run_safe(["make", "build"])) {
    print("Build succeeded!");
}
```

### Shell Injection Protection

```hemlock
// DANGEROUS: shell interprets the semicolon
run("echo " + user_input);  // user_input = "hello; rm -rf /"

// SAFE: no shell interpretation, semicolon is literal
run_safe(["echo", user_input]);  // Outputs: "hello; rm -rf /"
```

### When to Use Safe vs Regular Functions

| Use Case | Function | Reason |
|----------|----------|--------|
| User-provided arguments | `*_safe` functions | Prevents injection |
| Need stderr separate | `exec_safe`, `run_capture_safe` | Proper pipe separation |
| Shell features needed | `run`, `run_capture` | Pipes, redirects, globs |
| Simple commands | Either | Both work fine |

## Security Notes

- Always use `quote()` or `build_command()` for user input with shell functions
- **Prefer `*_safe` functions** (`exec_safe`, `run_safe`, etc.) for untrusted input
- Use `run(["cmd", arg1, arg2])` over string concatenation when using shell functions
- Validate paths before file operations
- Be cautious with `rm(..., true)` (recursive delete)
