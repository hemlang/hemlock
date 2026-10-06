# Command-Line Arguments in Hemlock

Hemlock programs can access command-line arguments via a built-in **`args` array** that is automatically populated at program startup.

## Table of Contents

- [Overview](#overview)
- [The args Array](#the-args-array)
- [Properties](#properties)
- [Iteration Patterns](#iteration-patterns)
- [Common Use Cases](#common-use-cases)
- [Argument Parsing Patterns](#argument-parsing-patterns)
- [Best Practices](#best-practices)
- [Complete Examples](#complete-examples)

## Overview

The `args` array provides access to command-line arguments passed to your Hemlock program:

- **Always available** - Built-in global variable in all Hemlock programs
- **Script name included** - `args[0]` always contains the script path/name
- **Array of strings** - All arguments are strings
- **Zero-indexed** - Standard array indexing (0, 1, 2, ...)

## The args Array

### Basic Structure

```hemlock
// args[0] is always the script filename
// args[1] through args[n-1] are the actual arguments
print(args[0]);        // "script.hml"
print(args.length);    // Total number of arguments (including script name)
```

### Example Usage

**Command:**
```bash
./hemlock script.hml hello world "test 123"
```

**In script.hml:**
```hemlock
print("Script name: " + args[0]);     // "script.hml"
print("Total args: " + args.length);  // "4"
print("First arg: " + args[1]);       // "hello"
print("Second arg: " + args[2]);      // "world"
print("Third arg: " + args[3]);       // "test 123"
```

### Index Reference

| Index | Contains | Example Value |
|-------|----------|---------------|
| `args[0]` | Script path/name | `"script.hml"` or `"./script.hml"` |
| `args[1]` | First argument | `"hello"` |
| `args[2]` | Second argument | `"world"` |
| `args[3]` | Third argument | `"test 123"` |
| ... | ... | ... |
| `args[n-1]` | Last argument | (varies) |

## Properties

### Always Present

`args` is a global array available in **all** Hemlock programs:

```hemlock
// No need to declare or import
print(args.length);  // Works immediately
```

### Script Name Included

`args[0]` always contains the script path/name:

```hemlock
print("Running: " + args[0]);
```

**Possible values for args[0]:**
- `"script.hml"` - Just the filename
- `"./script.hml"` - Relative path
- `"/home/user/script.hml"` - Absolute path
- Depends on how the script was invoked

### Type: Array of Strings

All arguments are stored as strings:

```hemlock
// Arguments: ./hemlock script.hml 42 3.14 true

print(args[1]);  // "42" (string, not number)
print(args[2]);  // "3.14" (string, not number)
print(args[3]);  // "true" (string, not boolean)

// Convert as needed:
let num = 42;  // Parse manually if needed
```

### Minimum Length

Always at least 1 (the script name):

```hemlock
print(args.length);  // Minimum: 1
```

**Even with no arguments:**
```bash
./hemlock script.hml
```

```hemlock
// In script.hml:
print(args.length);  // 1 (just script name)
```

### REPL Behavior

In the REPL, `args.length` is 0 (empty array):

```hemlock
# REPL session
> print(args.length);
0
```

## Iteration Patterns

### Basic Iteration

Skip `args[0]` (script name) and process actual arguments:

```hemlock
let i = 1;
while (i < args.length) {
    print("Argument " + i + ": " + args[i]);
    i = i + 1;
}
```

**Output for: `./hemlock script.hml foo bar baz`**
```
Argument 1: foo
Argument 2: bar
Argument 3: baz
```

### For-In Iteration (Including Script Name)

```hemlock
for (let arg in args) {
    print(arg);
}
```

**Output:**
```
script.hml
foo
bar
baz
```

### Checking Argument Count

```hemlock
if (args.length < 2) {
    print("Usage: " + args[0] + " <argument>");
    // exit or return
} else {
    let arg = args[1];
    // process arg
}
```

### Processing All Arguments Except Script Name

```hemlock
let actual_args = args.slice(1, args.length);

for (let arg in actual_args) {
    print("Processing: " + arg);
}
```

## Common Use Cases

### 1. Simple Argument Processing

Check for required argument:

```hemlock
if (args.length < 2) {
    print("Usage: " + args[0] + " <filename>");
} else {
    let filename = args[1];
    print("Processing file: " + filename);
    // ... process file
}
```

**Usage:**
```bash
./hemlock script.hml data.txt
# Output: Processing file: data.txt
```

### 2. Multiple Arguments

```hemlock
if (args.length < 3) {
    print("Usage: " + args[0] + " <input> <output>");
} else {
    let input_file = args[1];
    let output_file = args[2];

    print("Input: " + input_file);
    print("Output: " + output_file);

    // Process files...
}
```

**Usage:**
```bash
./hemlock convert.hml input.txt output.txt
```

### 3. Variable Number of Arguments

Process all provided arguments:

```hemlock
if (args.length < 2) {
    print("Usage: " + args[0] + " <file1> [file2] [file3] ...");
} else {
    print("Processing " + (args.length - 1) + " files:");

    let i = 1;
    while (i < args.length) {
        print("  " + args[i]);
        process_file(args[i]);
        i = i + 1;
    }
}
```

**Usage:**
```bash
./hemlock batch.hml file1.txt file2.txt file3.txt
```

### 4. Help Message

```hemlock
if (args.length < 2 || args[1] == "--help" || args[1] == "-h") {
    print("Usage: " + args[0] + " [OPTIONS] <file>");
    print("Options:");
    print("  -h, --help     Show this help message");
    print("  -v, --verbose  Enable verbose output");
} else {
    // Process normally
}
```

### 5. Argument Validation

```hemlock
fn validate_file(filename: string): bool {
    // Check if file exists (example)
    return filename != "";
}

if (args.length < 2) {
    print("Error: No filename provided");
} else if (!validate_file(args[1])) {
    print("Error: Invalid file: " + args[1]);
} else {
    print("Processing: " + args[1]);
}
```

## Argument Parsing Patterns

### Named Arguments (Flags)

Simple pattern for named arguments:

```hemlock
let verbose = false;
let output_file = "";
let input_file = "";

let i = 1;
while (i < args.length) {
    if (args[i] == "--verbose" || args[i] == "-v") {
        verbose = true;
    } else if (args[i] == "--output" || args[i] == "-o") {
        i = i + 1;
        if (i < args.length) {
            output_file = args[i];
        }
    } else {
        input_file = args[i];
    }
    i = i + 1;
}

if (verbose) {
    print("Verbose mode enabled");
}
print("Input: " + input_file);
print("Output: " + output_file);
```

**Usage:**
```bash
./hemlock script.hml --verbose --output out.txt input.txt
./hemlock script.hml -v -o out.txt input.txt
```

### Boolean Flags

```hemlock
let debug = false;
let verbose = false;
let force = false;

let i = 1;
while (i < args.length) {
    if (args[i] == "--debug") {
        debug = true;
    } else if (args[i] == "--verbose") {
        verbose = true;
    } else if (args[i] == "--force") {
        force = true;
    }
    i = i + 1;
}
```

### Value Arguments

```hemlock
let config_file = "default.conf";
let port = 8080;

let i = 1;
while (i < args.length) {
    if (args[i] == "--config") {
        i = i + 1;
        if (i < args.length) {
            config_file = args[i];
        }
    } else if (args[i] == "--port") {
        i = i + 1;
        if (i < args.length) {
            port = 8080;  // Would need to parse string to int
        }
    }
    i = i + 1;
}
```

### Mixed Positional and Named Arguments

```hemlock
let input_file = "";
let output_file = "";
let verbose = false;

let i = 1;
let positional = [];

while (i < args.length) {
    if (args[i] == "--verbose") {
        verbose = true;
    } else {
        // Treat as positional argument
        positional.push(args[i]);
    }
    i = i + 1;
}

// Assign positional arguments
if (positional.length > 0) {
    input_file = positional[0];
}
if (positional.length > 1) {
    output_file = positional[1];
}
```

### Argument Parser Helper Function

```hemlock
fn parse_args() {
    let options = {
        verbose: false,
        output: "",
        files: []
    };

    let i = 1;
    while (i < args.length) {
        let arg = args[i];

        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg == "--output" || arg == "-o") {
            i = i + 1;
            if (i < args.length) {
                options.output = args[i];
            }
        } else {
            // Positional argument
            options.files.push(arg);
        }

        i = i + 1;
    }

    return options;
}

let opts = parse_args();
print("Verbose: " + opts.verbose);
print("Output: " + opts.output);
print("Files: " + opts.files.length);
```

## Best Practices

### 1. Always Check Argument Count

```hemlock
// Good
if (args.length < 2) {
    print("Usage: " + args[0] + " <file>");
} else {
    process_file(args[1]);
}

// Bad - may crash if no arguments
process_file(args[1]);  // Error if args.length == 1
```

### 2. Provide Usage Information

```hemlock
fn show_usage() {
    print("Usage: " + args[0] + " [OPTIONS] <file>");
    print("Options:");
    print("  -h, --help     Show help");
    print("  -v, --verbose  Verbose output");
}

if (args.length < 2) {
    show_usage();
}
```

### 3. Validate Arguments

```hemlock
fn validate_args() {
    if (args.length < 2) {
        print("Error: Missing required argument");
        return false;
    }

    if (args[1] == "") {
        print("Error: Empty argument");
        return false;
    }

    return true;
}

if (!validate_args()) {
    // exit or show usage
}
```

### 4. Use Descriptive Variable Names

```hemlock
// Good
let input_filename = args[1];
let output_filename = args[2];
let max_iterations = args[3];

// Bad
let a = args[1];
let b = args[2];
let c = args[3];
```

### 5. Handle Quoted Arguments with Spaces

Shell automatically handles this:

```bash
./hemlock script.hml "file with spaces.txt"
```

```hemlock
print(args[1]);  // "file with spaces.txt"
```

### 6. Create Argument Objects

```hemlock
fn get_args() {
    return {
        script: args[0],
        input: args[1],
        output: args[2]
    };
}

let arguments = get_args();
print("Input: " + arguments.input);
```

## Complete Examples

### Example 1: File Processor

```hemlock
// Usage: ./hemlock process.hml <input> <output>

fn show_usage() {
    print("Usage: " + args[0] + " <input_file> <output_file>");
}

if (args.length < 3) {
    show_usage();
} else {
    let input = args[1];
    let output = args[2];

    print("Processing " + input + " -> " + output);

    // Process files
    let f_in = open(input, "r");
    let f_out = open(output, "w");

    try {
        let content = f_in.read();
        let processed = content.to_upper();  // Example processing
        f_out.write(processed);

        print("Done!");
    } finally {
        f_in.close();
        f_out.close();
    }
}
```

### Example 2: Batch File Processor

```hemlock
// Usage: ./hemlock batch.hml <file1> <file2> <file3> ...

if (args.length < 2) {
    print("Usage: " + args[0] + " <file1> [file2] [file3] ...");
} else {
    print("Processing " + (args.length - 1) + " files:");

    let i = 1;
    while (i < args.length) {
        let filename = args[i];
        print("  Processing: " + filename);

        try {
            let f = open(filename, "r");
            let content = f.read();
            f.close();

            // Process content...
            print("    " + content.length + " bytes");
        } catch (e) {
            print("    Error: " + e);
        }

        i = i + 1;
    }

    print("Done!");
}
```

### Example 3: Advanced Argument Parser

```hemlock
// Usage: ./hemlock app.hml [OPTIONS] <files...>
// Options:
//   --verbose, -v     Enable verbose output
//   --output, -o FILE Set output file
//   --help, -h        Show help

fn parse_arguments() {
    let config = {
        verbose: false,
        output: "output.txt",
        help: false,
        files: []
    };

    let i = 1;
    while (i < args.length) {
        let arg = args[i];

        if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
        } else if (arg == "--output" || arg == "-o") {
            i = i + 1;
            if (i < args.length) {
                config.output = args[i];
            } else {
                print("Error: --output requires a value");
            }
        } else if (arg == "--help" || arg == "-h") {
            config.help = true;
        } else if (arg.starts_with("--")) {
            print("Error: Unknown option: " + arg);
        } else {
            config.files.push(arg);
        }

        i = i + 1;
    }

    return config;
}

fn show_help() {
    print("Usage: " + args[0] + " [OPTIONS] <files...>");
    print("Options:");
    print("  --verbose, -v     Enable verbose output");
    print("  --output, -o FILE Set output file");
    print("  --help, -h        Show this help");
}

let config = parse_arguments();

if (config.help) {
    show_help();
} else if (config.files.length == 0) {
    print("Error: No input files specified");
    show_help();
} else {
    if (config.verbose) {
        print("Verbose mode enabled");
        print("Output file: " + config.output);
        print("Input files: " + config.files.length);
    }

    // Process files
    for (let file in config.files) {
        if (config.verbose) {
            print("Processing: " + file);
        }
        // ... process file
    }
}
```

### Example 4: Configuration Tool

```hemlock
// Usage: ./hemlock config.hml <action> [arguments]
// Actions:
//   get <key>
//   set <key> <value>
//   list

fn show_usage() {
    print("Usage: " + args[0] + " <action> [arguments]");
    print("Actions:");
    print("  get <key>         Get configuration value");
    print("  set <key> <value> Set configuration value");
    print("  list              List all configuration");
}

if (args.length < 2) {
    show_usage();
} else {
    let action = args[1];

    if (action == "get") {
        if (args.length < 3) {
            print("Error: 'get' requires a key");
        } else {
            let key = args[2];
            print("Getting: " + key);
            // ... get from config
        }
    } else if (action == "set") {
        if (args.length < 4) {
            print("Error: 'set' requires key and value");
        } else {
            let key = args[2];
            let value = args[3];
            print("Setting " + key + " = " + value);
            // ... set in config
        }
    } else if (action == "list") {
        print("Listing all configuration:");
        // ... list config
    } else {
        print("Error: Unknown action: " + action);
        show_usage();
    }
}
```

## Summary

Hemlock's command-line argument support provides:

- ✅ Built-in `args` array available globally
- ✅ Simple array-based access to arguments
- ✅ Script name in `args[0]`
- ✅ All arguments as strings
- ✅ Array methods available (.length, .slice, etc.)

Remember:
- Always check `args.length` before accessing elements
- `args[0]` is the script name
- Actual arguments start at `args[1]`
- All arguments are strings - convert as needed
- Provide usage information for user-friendly tools
- Validate arguments before processing

Common patterns:
- Simple positional arguments
- Named/flag arguments (--flag)
- Value arguments (--option value)
- Help messages (--help)
- Argument validation
