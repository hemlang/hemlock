# @stdlib/shell - Shell Utilities

Provides utilities for building shell commands safely, escaping arguments, parsing output, and executing commands. Includes Bash-like features such as brace expansion and range generation for shell scripting.

## Import

```hemlock
import { escape, quote, run, run_capture } from "@stdlib/shell";
import { which, command_exists, env_or } from "@stdlib/shell";
import { build_command, pipe, and_then } from "@stdlib/shell";
import { sh, sh_ok, last_exit } from "@stdlib/shell";
import { range, char_range, expand } from "@stdlib/shell";
```

## Quick Shell Execution

For quick scripting, use `sh()` to run shell commands directly (like Bash).

### sh(cmd: string): string

Run a shell command and return stdout (trimmed). Throws on failure.

```hemlock
// Simple command
let date = sh("date");
print(date);  // "Mon Jan 20 10:30:00 UTC 2025"

// Pipes work naturally
let count = sh("ls -la | wc -l");
print("Files: " + count);

// Command substitution
let kernel = sh("uname -r");
print("Kernel: " + kernel);

// Environment variables expand
let home = sh("echo $HOME");
```

### sh_ok(cmd: string): bool

Run a shell command, return true if it succeeded (doesn't throw).

```hemlock
if (sh_ok("which docker")) {
    print("Docker is installed");
}

if (sh_ok("test -f /etc/passwd")) {
    print("File exists");
}

// Check multiple conditions
if (sh_ok("[ -d /tmp ] && [ -w /tmp ]")) {
    print("/tmp is writable");
}
```

### last_exit(): i32

Get the exit code from the last `sh()` or `sh_ok()` call.

```hemlock
sh_ok("grep pattern file.txt");
if (last_exit() == 0) {
    print("Pattern found");
} else if (last_exit() == 1) {
    print("Pattern not found");
} else {
    print("Error occurred");
}
```

## Range Generation

Generate numeric and character sequences like Bash's `{1..10}`.

### range(start, end, step?): array

Generate a numeric range (inclusive).

```hemlock
range(1, 5);           // [1, 2, 3, 4, 5]
range(5, 1);           // [5, 4, 3, 2, 1] (auto-detects direction)
range(0, 10, 2);       // [0, 2, 4, 6, 8, 10]
range(10, 0, -2);      // [10, 8, 6, 4, 2, 0]

// Use in loops
for (i in range(1, 10)) {
    print("Count: " + i);
}

// Generate file indices
for (n in range(1, 100)) {
    let filename = "file" + n + ".txt";
    // ...
}
```

### char_range(start, end): array

Generate a character range.

```hemlock
char_range('a', 'z');  // ['a', 'b', ..., 'z']
char_range('A', 'F');  // ['A', 'B', 'C', 'D', 'E', 'F']
char_range('z', 'a');  // ['z', 'y', ..., 'a'] (reverse)
char_range('0', '9');  // ['0', '1', ..., '9']
```

## Brace Expansion

Expand Bash-style brace expressions.

### expand(pattern: string): array

Expand brace patterns like Bash.

```hemlock
// List expansion
expand("{a,b,c}");           // ["a", "b", "c"]
expand("{foo,bar,baz}");     // ["foo", "bar", "baz"]

// Numeric range
expand("{1..5}");            // ["1", "2", "3", "4", "5"]
expand("{5..1}");            // ["5", "4", "3", "2", "1"]
expand("{1..10..2}");        // ["1", "3", "5", "7", "9"]

// Character range
expand("{a..f}");            // ["a", "b", "c", "d", "e", "f"]
expand("{A..Z}");            // ["A", "B", ..., "Z"]

// Zero-padded ranges
expand("{01..05}");          // ["01", "02", "03", "04", "05"]
expand("{001..100}");        // ["001", "002", ..., "100"]

// With prefix and suffix
expand("file{1..3}.txt");    // ["file1.txt", "file2.txt", "file3.txt"]
expand("test_{a,b,c}.log");  // ["test_a.log", "test_b.log", "test_c.log"]

// Multiple braces (cartesian product)
expand("{a,b}{1,2}");        // ["a1", "a2", "b1", "b2"]
expand("{x,y}{1..3}");       // ["x1", "x2", "x3", "y1", "y2", "y3"]

// Nested braces
expand("{{a,b},{c,d}}");     // ["a", "b", "c", "d"]

// Real-world examples
expand("server{01..03}.example.com");
// ["server01.example.com", "server02.example.com", "server03.example.com"]

expand("backup_{mon,wed,fri}.tar.gz");
// ["backup_mon.tar.gz", "backup_wed.tar.gz", "backup_fri.tar.gz"]

expand("/var/log/{syslog,auth,kern}.log");
// ["/var/log/syslog.log", "/var/log/auth.log", "/var/log/kern.log"]
```

### Scripting Example

```hemlock
import { sh, expand, range } from "@stdlib/shell";

// Process multiple files
let files = expand("data_{2020..2024}.csv");
for (f in files) {
    print("Processing: " + f);
    sh("gzip " + f);
}

// Generate server list
let servers = expand("web{01..10}.prod.local");
for (server in servers) {
    if (sh_ok("ping -c 1 " + server)) {
        print(server + " is up");
    }
}

// Batch operations
for (i in range(1, 100)) {
    sh("curl -o page" + i + ".html https://example.com/page/" + i);
}
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
