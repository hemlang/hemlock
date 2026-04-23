# Hemlock Fuzz Testing

Fuzz testing for the Hemlock lexer and parser to find crashes, memory issues, and edge cases.

## Supported Fuzzing Frameworks

- **libFuzzer** (LLVM) - Fast, in-process coverage-guided fuzzing
- **AFL++** - Industry-standard coverage-guided fuzzing
- **Standalone** - For manual crash reproduction and debugging

## Quick Start

### Using libFuzzer (Recommended)

Requires clang with libFuzzer support:

```bash
# From project root
make fuzz-lexer    # Fuzz the lexer
make fuzz-parser   # Fuzz the parser
```

Or from this directory:

```bash
make fuzz-lexer-libfuzzer
make fuzz-parser-libfuzzer
```

Press `Ctrl+C` to stop. Crashes are saved to `crashes/`.

### Using AFL++

Install AFL++ first:
```bash
# Ubuntu/Debian
sudo apt install afl++

# macOS
brew install aflplusplus
```

Build and run:
```bash
# Build fuzzers
make fuzz-lexer-afl
make fuzz-parser-afl

# Run AFL++ (example for lexer)
mkdir -p crashes/lexer
afl-fuzz -i corpus -o crashes/lexer -- ./build/fuzz_lexer_afl @@
```

### Reproducing Crashes

Build standalone harnesses for crash analysis:

```bash
make standalone

# Run with crash file
./build/fuzz_lexer_standalone crashes/lexer/crash-xxx
./build/fuzz_parser_standalone crashes/parser/crash-xxx
```

## Directory Structure

```
tests/fuzz/
├── fuzz_lexer.c      # Lexer fuzz harness
├── fuzz_parser.c     # Parser fuzz harness
├── Makefile          # Build system
├── corpus/           # Seed inputs (valid Hemlock programs)
│   ├── literals.hml
│   ├── operators.hml
│   ├── control_flow.hml
│   └── ...
├── build/            # Compiled fuzzers (gitignored)
└── crashes/          # Found crashes (gitignored)
```

## Seed Corpus

The `corpus/` directory contains minimal valid Hemlock programs that exercise different language features:

| File | Features |
|------|----------|
| `literals.hml` | Numbers, strings, booleans, null, runes |
| `operators.hml` | Arithmetic, comparison, logical, bitwise |
| `control_flow.hml` | if/else, while, for, break/continue |
| `functions.hml` | Function definitions, closures, typed params |
| `arrays.hml` | Array literals, indexing, methods |
| `objects.hml` | Object definitions, literals, field access |
| `strings.hml` | String methods, template strings |
| `enums.hml` | Enum definitions |
| `exceptions.hml` | try/catch/finally, throw |
| `switch.hml` | Switch statements |
| `numbers.hml` | All numeric types, hex/binary literals |
| `for_in.hml` | For-in iteration |
| `defer.hml` | Defer statements |
| `optional.hml` | Optional chaining, null coalescing |
| `ternary.hml` | Ternary expressions |
| `increment.hml` | Prefix/postfix increment/decrement |
| `async.hml` | Async functions, spawn, await |
| `typed_array.hml` | Typed arrays |

## Writing Custom Seeds

Add new `.hml` files to `corpus/` to improve fuzzing coverage:

```hemlock
// corpus/my_feature.hml
let x = some_new_feature();
```

Good seeds are:
- Small (< 1KB ideally)
- Valid Hemlock programs
- Exercise specific language features
- Cover edge cases

## Interpreting Results

### libFuzzer Output

```
#12345  NEW    cov: 1234 ft: 5678 corp: 42/1234b exec/s: 9999
```

- `NEW` - Found new coverage
- `cov` - Basic block coverage count
- `corp` - Corpus size / total bytes
- `exec/s` - Executions per second

### AFL++ Output

Watch the `unique_crashes` and `unique_hangs` counters.

## Sanitizers

All fuzz targets are built with:
- **AddressSanitizer (ASan)** - Detects memory errors
- **UndefinedBehaviorSanitizer (UBSan)** - Detects undefined behavior

## Cleaning Up

```bash
make clean          # Remove build artifacts
make clean-crashes  # Remove crash files
make clean-all      # Remove everything
```

## Troubleshooting

### "clang not found"
Install clang: `sudo apt install clang` or `brew install llvm`

### "afl-clang-fast not found"
Install AFL++: `sudo apt install afl++` or `brew install aflplusplus`

### Fuzzer runs out of memory
The harnesses limit input size to 1MB. For larger limits, modify `LLVMFuzzerTestOneInput`.

### Crash doesn't reproduce
Ensure you're using the same sanitizer flags when building standalone:
```bash
make standalone
./build/fuzz_lexer_standalone crash_file 2>&1 | less
```
