# Hemlock Python Module

A standard library module providing Python interoperability for Hemlock programs via FFI to the Python C API.

## Overview

The python module enables Hemlock programs to:

- **Import Python modules** - Use any Python package (NumPy, Transformers, PyTorch, etc.)
- **Call Python functions** - Execute Python code with full argument passing
- **Convert types** - Seamlessly convert between Hemlock and Python types
- **Handle errors** - Catch Python exceptions as Hemlock exceptions
- **Manage GIL** - Thread-safe Python access from async Hemlock code
- **NumPy integration** - Zero-copy buffer sharing with NumPy arrays

## Quick Start

```hemlock
import { py_init, py_import, py_call, py_get, from_python, py_finalize } from "@stdlib/python";

py_init();

// Use Python's math module
let math = py_import("math");
let sqrt = py_get(math, "sqrt");
let result = py_call(sqrt, [16]);
print(from_python(result));  // 4.0

py_finalize();
```

## Usage with Hugging Face Transformers

```hemlock
import { py_init, py_import, py_call, py_get, from_python, py_release, py_finalize } from "@stdlib/python";

py_init();

// Load transformers
let transformers = py_import("transformers");
let pipeline_fn = py_get(transformers, "pipeline");

// Create sentiment analysis pipeline
let classifier = py_call(pipeline_fn, ["sentiment-analysis"]);

// Analyze text
let texts = ["I love Hemlock!", "This is terrible."];
for (text in texts) {
    let result = py_call(classifier, [text]);
    let analysis = from_python(result);
    print(text + " -> " + analysis[0].label + " (" + analysis[0].score + ")");
    py_release(result);
}

py_release(classifier);
py_release(pipeline_fn);
py_release(transformers);
py_finalize();
```

---

## Initialization

### py_init(options?)

Initializes the Python interpreter. Must be called before any other Python functions.

**Parameters:**
- `options: object` (optional) - Configuration options:
  - `path: string` - Additional path to add to sys.path

**Returns:** `bool` - True on success

**Throws:** Exception if initialization fails

```hemlock
import { py_init, py_finalize } from "@stdlib/python";

// Basic initialization
py_init();

// With options
py_init({ path: "/custom/python/modules" });

// Always finalize when done
py_finalize();
```

### py_is_initialized()

Checks if Python is initialized.

**Returns:** `bool` - True if Python is initialized

### py_finalize()

Finalizes the Python interpreter and releases all resources.

**Returns:** `null`

### py_version()

Gets the Python version string.

**Returns:** `string` - Version string (e.g., "3.11.4")

```hemlock
import { py_init, py_version, py_finalize } from "@stdlib/python";

py_init();
print("Python " + py_version());  // "Python 3.11.4 (main, ...)"
py_finalize();
```

---

## Module Import

### py_import(module_name)

Imports a Python module by name.

**Parameters:**
- `module_name: string` - Name of the module to import

**Returns:** `PyObj` - The imported module

**Throws:** Exception if import fails

```hemlock
import { py_init, py_import, py_release, py_finalize } from "@stdlib/python";

py_init();

let math = py_import("math");
let numpy = py_import("numpy");
let transformers = py_import("transformers");

// Submodule import
let path = py_import("os.path");

py_release(math);
py_release(numpy);
py_release(transformers);
py_release(path);

py_finalize();
```

### py_from_import(module_name, name)

Imports a specific name from a module (equivalent to `from module import name`).

**Parameters:**
- `module_name: string` - Name of the module
- `name: string` - Name to import from the module

**Returns:** `PyObj` - The imported object

```hemlock
import { py_init, py_from_import, py_call, from_python, py_finalize } from "@stdlib/python";

py_init();

// from math import sqrt
let sqrt = py_from_import("math", "sqrt");
let result = py_call(sqrt, [25]);
print(from_python(result));  // 5.0

py_finalize();
```

### py_add_path(path)

Adds a directory to sys.path for importing modules.

**Parameters:**
- `path: string` - Directory path to add

```hemlock
import { py_init, py_add_path, py_import, py_finalize } from "@stdlib/python";

py_init();
py_add_path("/my/custom/modules");
let my_module = py_import("my_module");
py_finalize();
```

---

## Object Access

### py_get(obj, attr_name)

Gets an attribute from a Python object.

**Parameters:**
- `obj: PyObj` - Python object
- `attr_name: string` - Attribute name

**Returns:** `PyObj` - The attribute value

```hemlock
import { py_init, py_import, py_get, from_python, py_finalize } from "@stdlib/python";

py_init();

let math = py_import("math");
let pi = py_get(math, "pi");
print(from_python(pi));  // 3.141592653589793

py_finalize();
```

### py_set(obj, attr_name, value)

Sets an attribute on a Python object.

**Parameters:**
- `obj: PyObj` - Python object
- `attr_name: string` - Attribute name
- `value` - Value to set (automatically converted to Python)

```hemlock
import { py_init, py_import, py_set, py_finalize } from "@stdlib/python";

py_init();

let obj = py_eval("type('MyClass', (), {})()");
py_set(obj, "name", "Hello");
py_set(obj, "value", 42);

py_finalize();
```

### py_has(obj, attr_name)

Checks if an object has an attribute.

**Parameters:**
- `obj: PyObj` - Python object
- `attr_name: string` - Attribute name

**Returns:** `bool` - True if attribute exists

### py_getitem(obj, key)

Gets an item from a sequence or mapping (equivalent to `obj[key]`).

**Parameters:**
- `obj: PyObj` - Python sequence or mapping
- `key` - Index or key

**Returns:** `PyObj` - The item

```hemlock
import { py_init, py_eval, py_getitem, from_python, py_finalize } from "@stdlib/python";

py_init();

let my_list = py_eval("[10, 20, 30]");
let item = py_getitem(my_list, 1);
print(from_python(item));  // 20

let my_dict = py_eval("{'a': 1, 'b': 2}");
let value = py_getitem(my_dict, "a");
print(from_python(value));  // 1

py_finalize();
```

### py_setitem(obj, key, value)

Sets an item in a sequence or mapping.

### py_delitem(obj, key)

Deletes an item from a sequence or mapping.

### py_len(obj)

Gets the length of a sequence or mapping.

**Returns:** `i64` - Length

---

## Function Calls

### py_call(callable, args?)

Calls a Python callable with positional arguments.

**Parameters:**
- `callable: PyObj` - Python callable (function, method, class, etc.)
- `args: array` (optional) - Positional arguments (auto-converted to Python)

**Returns:** `PyObj` - Return value

```hemlock
import { py_init, py_import, py_get, py_call, from_python, py_finalize } from "@stdlib/python";

py_init();

let math = py_import("math");
let pow_fn = py_get(math, "pow");

// Call with two arguments
let result = py_call(pow_fn, [2, 10]);
print(from_python(result));  // 1024.0

py_finalize();
```

### py_call_method(obj, method_name, args?)

Calls a method on an object.

**Parameters:**
- `obj: PyObj` - Python object
- `method_name: string` - Method name
- `args: array` (optional) - Arguments

**Returns:** `PyObj` - Return value

```hemlock
import { py_init, py_eval, py_call_method, from_python, py_finalize } from "@stdlib/python";

py_init();

let my_list = py_eval("[3, 1, 4, 1, 5]");
py_call_method(my_list, "sort", []);
print(from_python(my_list));  // [1, 1, 3, 4, 5]

let my_str = py_eval("'hello world'");
let upper = py_call_method(my_str, "upper", []);
print(from_python(upper));  // "HELLO WORLD"

py_finalize();
```

### py_call_kw(callable, args?, kwargs?)

Calls a Python callable with keyword arguments.

**Parameters:**
- `callable: PyObj` - Python callable
- `args: array` (optional) - Positional arguments
- `kwargs: object` (optional) - Keyword arguments as Hemlock object

**Returns:** `PyObj` - Return value

```hemlock
import { py_init, py_import, py_get, py_call_kw, from_python, py_finalize } from "@stdlib/python";

py_init();

let json = py_import("json");
let dumps = py_get(json, "dumps");

// json.dumps(obj, indent=2, sort_keys=True)
let obj = { name: "Alice", age: 30 };
let result = py_call_kw(dumps, [obj], { indent: 2, sort_keys: true });
print(from_python(result));

py_finalize();
```

---

## Type Conversion

### to_python(value)

Converts a Hemlock value to a Python object.

**Parameters:**
- `value` - Any Hemlock value

**Returns:** `PyObj` - Python equivalent

**Type Conversion Table (Hemlock to Python):**

| Hemlock Type | Python Type |
|--------------|-------------|
| `null` | `None` |
| `bool` | `bool` |
| `i8`, `i16`, `i32`, `i64` | `int` |
| `u8`, `u16`, `u32`, `u64` | `int` |
| `f32`, `f64` | `float` |
| `string` | `str` |
| `array` | `list` |
| `object` | `dict` |
| `buffer` | `bytes` |
| `PyObj` | (passthrough) |

### from_python(obj)

Converts a Python object to a Hemlock value.

**Parameters:**
- `obj: PyObj` - Python object

**Returns:** Appropriate Hemlock type

**Type Conversion Table (Python to Hemlock):**

| Python Type | Hemlock Type |
|-------------|--------------|
| `None` | `null` |
| `bool` | `bool` |
| `int` | `i64` |
| `float` | `f64` |
| `str` | `string` |
| `bytes` | `buffer` |
| `list` | `array` |
| `tuple` | `array` |
| `dict` | `object` |
| Other | `PyObj` (wrapped) |

```hemlock
import { py_init, to_python, from_python, py_finalize } from "@stdlib/python";

py_init();

// Hemlock to Python
let py_list = to_python([1, 2, 3]);
let py_dict = to_python({ name: "Alice", age: 30 });

// Python to Hemlock
let hml_list = from_python(py_list);  // [1, 2, 3]
let hml_dict = from_python(py_dict);  // { name: "Alice", age: 30 }

py_finalize();
```

### Explicit Conversion Functions

For more control over type conversion:

```hemlock
py_to_int(obj: PyObj): i64
py_to_float(obj: PyObj): f64
py_to_string(obj: PyObj): string
py_to_bool(obj: PyObj): bool
py_to_list(obj: PyObj): array
py_to_dict(obj: PyObj): object
```

---

## Type Checking

### py_type(obj)

Gets the Python type name of an object.

**Returns:** `string` - Type name (e.g., "int", "str", "list")

```hemlock
import { py_init, py_eval, py_type, py_finalize } from "@stdlib/python";

py_init();

print(py_type(py_eval("42")));        // "int"
print(py_type(py_eval("'hello'")));   // "str"
print(py_type(py_eval("[1,2,3]")));   // "list"

py_finalize();
```

### Type Check Functions

```hemlock
py_is_none(obj: PyObj): bool
py_is_callable(obj: PyObj): bool
py_is_sequence(obj: PyObj): bool
py_is_mapping(obj: PyObj): bool
py_is_int(obj: PyObj): bool
py_is_float(obj: PyObj): bool
py_is_string(obj: PyObj): bool
py_is_list(obj: PyObj): bool
py_is_dict(obj: PyObj): bool
py_is_tuple(obj: PyObj): bool
```

---

## Collection Builders

### py_list(items?)

Creates a Python list from a Hemlock array.

### py_dict(items?)

Creates a Python dict from a Hemlock object.

### py_tuple(items?)

Creates a Python tuple from a Hemlock array.

### py_set_create(items?)

Creates a Python set from a Hemlock array.

```hemlock
import { py_init, py_list, py_dict, py_tuple, py_set_create, py_finalize } from "@stdlib/python";

py_init();

let my_list = py_list([1, 2, 3]);
let my_dict = py_dict({ a: 1, b: 2 });
let my_tuple = py_tuple([1, 2, 3]);
let my_set = py_set_create([1, 2, 2, 3]);  // {1, 2, 3}

py_finalize();
```

---

## Error Handling

### py_error_occurred()

Checks if a Python exception occurred.

**Returns:** `bool` - True if exception occurred

### py_error_fetch()

Gets the current Python exception as a `PyError` object.

**Returns:** `PyError` with fields:
- `type: string` - Exception type name
- `message: string` - Exception message
- `traceback: string` - Traceback info

### py_error_clear()

Clears the current Python exception.

### py_check_error()

Checks for Python exception and throws Hemlock exception if present.

```hemlock
import { py_init, py_import, py_call, py_get, from_python, py_finalize } from "@stdlib/python";

py_init();

try {
    let math = py_import("math");
    let sqrt = py_get(math, "sqrt");
    let result = py_call(sqrt, [-1]);  // Raises ValueError in Python
    print(from_python(result));
} catch (e) {
    print("Caught: " + e);  // "Caught: Python ValueError: math domain error"
}

py_finalize();
```

---

## Memory Management

### py_incref(obj)

Manually increments reference count.

### py_decref(obj)

Manually decrements reference count.

### py_release(obj)

Releases a PyObj (decrefs if owned). Use this when done with a Python object.

```hemlock
import { py_init, py_import, py_get, py_release, py_finalize } from "@stdlib/python";

py_init();

let math = py_import("math");
let pi = py_get(math, "pi");

// Use pi...

// Release when done
py_release(pi);
py_release(math);

py_finalize();
```

**Best Practice:** Always release Python objects when done to prevent memory leaks.

---

## GIL Management

For multi-threaded Hemlock programs using Python.

### py_gil_acquire()

Acquires the Global Interpreter Lock.

**Returns:** `PyGILState` - State handle for releasing

### py_gil_release(state)

Releases the GIL.

**Parameters:**
- `state: PyGILState` - State from `py_gil_acquire`

### py_with_gil(callback)

Executes a function with the GIL held. Automatically acquires and releases.

**Parameters:**
- `callback: fn` - Function to execute

**Returns:** Return value of callback

```hemlock
import { py_init, py_import, py_call, py_get, py_with_gil, from_python, py_finalize } from "@stdlib/python";

py_init();

// Thread-safe Python access from async context
async fn analyze(text: string) {
    return py_with_gil(fn() {
        let transformers = py_import("transformers");
        let pipeline = py_call(py_get(transformers, "pipeline"), ["sentiment-analysis"]);
        return from_python(py_call(pipeline, [text]));
    });
}

let task = spawn(analyze, "This is great!");
let result = await task;
print(result);

py_finalize();
```

---

## NumPy Integration

### numpy_available()

Checks if NumPy is installed and importable.

**Returns:** `bool` - True if NumPy is available

### numpy_from_buffer(buf, shape, dtype?)

Creates a NumPy array from a Hemlock buffer.

**Parameters:**
- `buf: buffer` - Raw data buffer
- `shape: array` - Array shape (e.g., `[224, 224, 3]`)
- `dtype: string` (optional) - Data type, default "float64"

**Returns:** `PyObj` - NumPy array

```hemlock
import { py_init, numpy_from_buffer, numpy_shape, py_finalize } from "@stdlib/python";

py_init();

// Create a 2x2 float32 array
let data = buffer(16);  // 4 floats * 4 bytes
ptr_write_f32(data, 1.0);
ptr_write_f32(data + 4, 2.0);
ptr_write_f32(data + 8, 3.0);
ptr_write_f32(data + 12, 4.0);

let arr = numpy_from_buffer(data, [2, 2], "float32");
print(numpy_shape(arr));  // [2, 2]

py_finalize();
```

### numpy_to_buffer(arr)

Gets NumPy array data as a Hemlock buffer.

**Parameters:**
- `arr: PyObj` - NumPy array

**Returns:** `buffer` - Raw data

### numpy_shape(arr)

Gets the shape of a NumPy array.

**Returns:** `array` - Shape dimensions

### numpy_dtype(arr)

Gets the data type name of a NumPy array.

**Returns:** `string` - dtype name (e.g., "float32", "int64")

### numpy_to_array(arr)

Converts a NumPy array to a `NumpyArray` struct with full metadata.

**Returns:** `NumpyArray` with fields:
- `data: buffer` - Raw data
- `shape: array` - Dimensions
- `dtype: string` - Data type
- `strides: array` - Byte strides

```hemlock
import { py_init, py_import, py_call, py_get, numpy_to_array, py_finalize } from "@stdlib/python";

py_init();

let np = py_import("numpy");
let arr = py_call(py_get(np, "zeros"), [[3, 4]]);

let info = numpy_to_array(arr);
print("Shape:", info.shape);    // [3, 4]
print("Dtype:", info.dtype);    // "float64"
print("Strides:", info.strides);

py_finalize();
```

---

## Convenience Functions

### py_eval(expr)

Evaluates a Python expression string.

**Parameters:**
- `expr: string` - Python expression

**Returns:** `PyObj` - Result

```hemlock
import { py_init, py_eval, from_python, py_finalize } from "@stdlib/python";

py_init();

let result = py_eval("2 ** 10");
print(from_python(result));  // 1024

let list = py_eval("[x**2 for x in range(5)]");
print(from_python(list));  // [0, 1, 4, 9, 16]

py_finalize();
```

### py_exec(code)

Executes Python code (doesn't return a value).

**Parameters:**
- `code: string` - Python code

```hemlock
import { py_init, py_exec, py_import, py_get, from_python, py_finalize } from "@stdlib/python";

py_init();

py_exec("
import sys
sys.my_var = 42
");

let sys = py_import("sys");
let my_var = py_get(sys, "my_var");
print(from_python(my_var));  // 42

py_finalize();
```

### py_repr(obj)

Gets the repr() string of a Python object.

**Returns:** `string` - Repr string

### py_str(obj)

Gets the str() string of a Python object.

**Returns:** `string` - String representation

### py_print(obj)

Prints a Python object (for debugging).

---

## Types

### PyObj

Wrapper for a Python object with reference counting.

```hemlock
define PyObj {
    _ptr: ptr,       // PyObject* pointer
    _owned: bool,    // Whether we own the reference
}
```

### PyError

Python exception information.

```hemlock
define PyError {
    type: string,        // Exception type name
    message: string,     // Exception message
    traceback: string,   // Traceback info
}
```

### PyGILState

GIL state handle for multi-threaded access.

```hemlock
define PyGILState {
    _state: i32,
}
```

### NumpyArray

NumPy array metadata.

```hemlock
define NumpyArray {
    data: buffer,        // Raw data buffer
    shape: array,        // Dimensions
    dtype: string,       // Data type
    strides: array,      // Byte strides
}
```

---

## Examples

### Using scikit-learn

```hemlock
import { py_init, py_import, py_call, py_get, py_call_method, from_python, py_finalize } from "@stdlib/python";

py_init();

// Import sklearn
let sklearn = py_import("sklearn.linear_model");
let LinearRegression = py_get(sklearn, "LinearRegression");

// Create model
let model = py_call(LinearRegression, []);

// Training data
let X = [[1], [2], [3], [4]];
let y = [2, 4, 6, 8];

// Fit model
py_call_method(model, "fit", [X, y]);

// Predict
let predictions = py_call_method(model, "predict", [[[5], [6]]]);
print(from_python(predictions));  // [10.0, 12.0]

py_finalize();
```

### Using Pandas

```hemlock
import { py_init, py_import, py_call, py_get, py_call_kw, from_python, py_finalize } from "@stdlib/python";

py_init();

let pd = py_import("pandas");
let DataFrame = py_get(pd, "DataFrame");

// Create DataFrame from dict
let data = {
    name: ["Alice", "Bob", "Charlie"],
    age: [30, 25, 35],
    city: ["NYC", "LA", "Chicago"]
};

let df = py_call(DataFrame, [data]);

// Filter
let filtered = py_call_method(df, "query", ["age > 25"]);
print(py_call_method(filtered, "to_string", []));

py_finalize();
```

### Image Processing with PIL

```hemlock
import { py_init, py_import, py_call, py_get, py_call_method, py_finalize } from "@stdlib/python";

py_init();

let Image = py_from_import("PIL", "Image");

// Open image
let img = py_call_method(Image, "open", ["photo.jpg"]);

// Resize
let resized = py_call_method(img, "resize", [[200, 200]]);

// Save
py_call_method(resized, "save", ["photo_resized.jpg"]);

py_finalize();
```

---

## System Requirements

- Python 3.8+ with development headers
- libpython3.x.so shared library

**Installation:**

```bash
# Debian/Ubuntu
sudo apt-get install python3-dev

# Fedora/RHEL
sudo dnf install python3-devel

# macOS
brew install python3

# Arch Linux
sudo pacman -S python
```

**Note:** The module imports `libpython3.11.so.1.0` by default. You may need to modify the import statement in `python.hml` or create a symlink for your Python version.

---

## Performance Tips

1. **Cache imported modules** - Store module references instead of re-importing
2. **Batch operations** - Minimize Python/Hemlock boundary crossings
3. **Use NumPy buffers** - Share data via buffer protocol for zero-copy
4. **Release objects** - Call `py_release()` when done to free memory
5. **Use GIL wisely** - For parallel code, minimize time holding the GIL

---

## Troubleshooting

### Import Error: libpython3.x.so not found

Ensure Python development libraries are installed and the library is in your library path:

```bash
# Find your Python library
find /usr -name "libpython3*.so*" 2>/dev/null

# Add to library path if needed
export LD_LIBRARY_PATH=/usr/lib/python3.11:$LD_LIBRARY_PATH
```

### Segmentation Fault

- Always call `py_init()` before any Python operations
- Always call `py_finalize()` when done
- Don't use Python objects after `py_finalize()`
- Release objects in reverse order of acquisition

### Memory Leaks

- Always call `py_release()` on Python objects when done
- Use `defer` for automatic cleanup:

```hemlock
let module = py_import("mymodule");
defer py_release(module);
// ... use module ...
```

---

## See Also

- [Python C API Documentation](https://docs.python.org/3/c-api/index.html)
- [NumPy C API](https://numpy.org/doc/stable/reference/c-api/)
- `@stdlib/ffi` - Low-level FFI operations
- `@stdlib/async` - Async/concurrency utilities

---

## License

Part of the Hemlock standard library.
