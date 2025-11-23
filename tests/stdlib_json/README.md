# JSON Module Tests

Comprehensive test suite for `@stdlib/json` module.

## Test Files

### 1. `parse_stringify_test.hml`
Tests core parsing and serialization functionality:
- Parse JSON primitives (null, bool, number, string)
- Parse JSON arrays and objects
- Parse nested structures
- Stringify all JSON types
- Round-trip testing (parse → stringify → parse)
- Error handling (invalid JSON, circular references)

**Run:**
```bash
./hemlock tests/stdlib_json/parse_stringify_test.hml
```

### 2. `pretty_test.hml`
Tests pretty printing and formatting:
- Pretty print objects with default indentation
- Pretty print arrays with formatting
- Pretty print nested structures
- Custom indentation (4 spaces, tabs)
- Empty objects and arrays
- String escaping in formatted output

**Run:**
```bash
./hemlock tests/stdlib_json/pretty_test.hml
```

### 3. `path_test.hml`
Tests path access with dot notation:
- `get()` - Nested object access
- `get()` - Array index access
- `get()` - Default values for missing paths
- `get()` - Empty path returns root
- `set()` - Modify nested properties
- `set()` - Modify array elements
- `has()` - Check path existence
- `delete()` - Remove properties (sets to null)

**Run:**
```bash
./hemlock tests/stdlib_json/path_test.hml
```

### 4. `validation_test.hml`
Tests validation and type checking:
- `is_valid()` - Quick JSON validation
- `validate()` - Detailed validation with error messages
- `is_object()`, `is_array()`, `is_string()` - Type predicates
- `is_number()`, `is_bool()`, `is_null()` - Type predicates
- `type_of()` - Get JSON type as string

**Run:**
```bash
./hemlock tests/stdlib_json/validation_test.hml
```

### 5. `clone_equals_test.hml`
Tests cloning and comparison:
- `clone()` - Deep copy primitives
- `clone()` - Deep copy arrays and objects
- `clone()` - Verify independence (modifications don't affect original)
- `clone()` - Nested structures
- `equals()` - Primitive comparison
- `equals()` - Array comparison (deep)
- `equals()` - Type mismatch detection

**Run:**
```bash
./hemlock tests/stdlib_json/clone_equals_test.hml
```

### 6. `integration_test.hml`
Real-world usage scenarios:
- Configuration file manipulation
- API response processing
- Data transformation with clone
- Validation workflows
- Pretty printing for debugging
- Array comparison
- Complex nested structures
- File I/O (read/write JSON files)

**Run:**
```bash
./hemlock tests/stdlib_json/integration_test.hml
```

## Run All Tests

```bash
# Via make test
make test | grep stdlib_json

# Or run individually
./hemlock tests/stdlib_json/parse_stringify_test.hml
./hemlock tests/stdlib_json/pretty_test.hml
./hemlock tests/stdlib_json/path_test.hml
./hemlock tests/stdlib_json/validation_test.hml
./hemlock tests/stdlib_json/clone_equals_test.hml
./hemlock tests/stdlib_json/integration_test.hml
```

## Test Coverage

### Functionality Coverage
- ✅ Parsing (parse, parse_file)
- ✅ Serialization (stringify, stringify_file)
- ✅ Pretty printing (pretty, pretty_file)
- ✅ Path access (get, set, has, delete)
- ✅ Validation (is_valid, validate)
- ✅ Type checking (is_*, type_of)
- ✅ Utilities (clone, equals)
- ✅ Error handling (all exception cases)
- ✅ Edge cases (empty, null, nested, circular)

### Test Scenarios
- ✅ All JSON types (primitives, arrays, objects)
- ✅ Nested structures (deep objects, nested arrays)
- ✅ Array indexing in paths
- ✅ Missing paths with defaults
- ✅ Type mismatches
- ✅ Invalid JSON
- ✅ Circular references
- ✅ Empty structures
- ✅ String escaping
- ✅ File I/O
- ✅ Real-world use cases

## Known Limitations in Tests

Some functions are tested but have incomplete implementation due to missing builtins:
- `merge()` - Requires `object_keys()` builtin (throws "not implemented" error)
- `patch()` - Requires `object_keys()` builtin (throws "not implemented" error)
- `equals()` for objects - Requires `object_keys()` builtin (works for arrays/primitives)

These limitations are documented and tests will be added when builtins are available.

## Test Assertions

All tests use Hemlock's built-in `assert()` function:
```hemlock
assert(condition, "error message");
```

Tests print "All X tests passed!" on success.

## Expected Output

Each test file should print:
```
All [feature] tests passed!
```

No output indicates test failure (assertion error).
