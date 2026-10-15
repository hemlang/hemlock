# Hemlock Matrix Module

A standard library module providing dense matrix operations for linear algebra and numeric computation.

## Overview

The matrix module provides:

- **Matrix creation** - Constructor, identity, zeros, ones, diagonal, from arrays
- **Element access** - get, set, row, col extraction
- **Arithmetic** - add, sub, mul, scale, negate, hadamard
- **Linear algebra** - det, inverse, solve, trace, norm, dot
- **Utilities** - transpose, clone, submatrix, map, equals, to_string

## Usage

```hemlock
import { Matrix, identity, from_rows } from "@stdlib/matrix";

let m = Matrix(2, 3);
m.set(0, 0, 1.0);
m.set(0, 1, 2.0);
print(m.get(0, 1));  // 2.0
```

Or import all:

```hemlock
import * as matrix from "@stdlib/matrix";
let I = matrix.identity(3);
```

---

## Matrix Constructor

### Matrix(rows, cols)

Creates a new matrix initialized to zero.

**Parameters:**
- `rows: i32` - Number of rows (must be positive)
- `cols: i32` - Number of columns (must be positive)

**Returns:** Matrix object

**Throws:** If rows or cols are not positive

```hemlock
import { Matrix } from "@stdlib/matrix";

let m = Matrix(3, 4);  // 3x4 zero matrix
m.set(0, 0, 5.0);
print(m.get(0, 0));  // 5.0
print(m.rows);        // 3
print(m.cols);        // 4
```

---

## Factory Functions

### from_array(rows, cols, values)

Creates a matrix from a flat array in row-major order.

**Parameters:**
- `rows: i32` - Number of rows
- `cols: i32` - Number of columns
- `values: array` - Flat array of values (length must equal rows * cols)

**Returns:** Matrix object

```hemlock
import { from_array } from "@stdlib/matrix";

let m = from_array(2, 3, [1, 2, 3, 4, 5, 6]);
// [[1, 2, 3]
//  [4, 5, 6]]
```

### from_rows(row_arrays)

Creates a matrix from an array of row arrays.

**Parameters:**
- `row_arrays: array` - Array of arrays, each representing a row

**Returns:** Matrix object

**Throws:** If rows have different lengths or input is empty

```hemlock
import { from_rows } from "@stdlib/matrix";

let m = from_rows([[1, 2, 3], [4, 5, 6]]);
print(m.get(1, 2));  // 6
```

### identity(n)

Creates an NxN identity matrix.

**Parameters:**
- `n: i32` - Size of the matrix

**Returns:** Matrix object with 1s on the diagonal, 0s elsewhere

```hemlock
import { identity } from "@stdlib/matrix";

let I = identity(3);
// [[1, 0, 0]
//  [0, 1, 0]
//  [0, 0, 1]]
```

### zeros(rows, cols)

Creates a matrix filled with zeros.

**Parameters:**
- `rows: i32` - Number of rows
- `cols: i32` - Number of columns

**Returns:** Matrix object

```hemlock
import { zeros } from "@stdlib/matrix";

let z = zeros(2, 3);  // 2x3 zero matrix
```

### ones(rows, cols)

Creates a matrix filled with ones.

**Parameters:**
- `rows: i32` - Number of rows
- `cols: i32` - Number of columns

**Returns:** Matrix object

```hemlock
import { ones } from "@stdlib/matrix";

let m = ones(2, 2);
print(m.get(0, 0));  // 1.0
```

### diagonal(values)

Creates a diagonal matrix from an array of values.

**Parameters:**
- `values: array` - Values to place on the diagonal

**Returns:** NxN Matrix object where N is the array length

```hemlock
import { diagonal } from "@stdlib/matrix";

let d = diagonal([2, 3, 4]);
// [[2, 0, 0]
//  [0, 3, 0]
//  [0, 0, 4]]
```

### col_vector(values)

Creates an Nx1 column vector from an array.

**Parameters:**
- `values: array` - Vector elements

**Returns:** Nx1 Matrix object

```hemlock
import { col_vector } from "@stdlib/matrix";

let v = col_vector([1, 2, 3]);
print(v.rows);  // 3
print(v.cols);  // 1
```

### row_vector(values)

Creates a 1xN row vector from an array.

**Parameters:**
- `values: array` - Vector elements

**Returns:** 1xN Matrix object

```hemlock
import { row_vector } from "@stdlib/matrix";

let v = row_vector([1, 2, 3]);
print(v.rows);  // 1
print(v.cols);  // 3
```

---

## Matrix Methods

### get(r, c)

Gets the element at row r, column c.

**Parameters:**
- `r: i32` - Row index (0-based)
- `c: i32` - Column index (0-based)

**Returns:** `f64` - Element value

**Throws:** If indices are out of bounds

### set(r, c, val)

Sets the element at row r, column c.

**Parameters:**
- `r: i32` - Row index (0-based)
- `c: i32` - Column index (0-based)
- `val: f64` - Value to set

**Throws:** If indices are out of bounds

### row(r)

Returns a row as an array.

**Parameters:**
- `r: i32` - Row index

**Returns:** `array` - Copy of the row

### col(c)

Returns a column as an array.

**Parameters:**
- `c: i32` - Column index

**Returns:** `array` - Copy of the column

### to_array()

Returns a flat copy of all elements in row-major order.

**Returns:** `array` - Flat array of all elements

### clone()

Returns a deep copy of the matrix.

**Returns:** New Matrix with identical dimensions and values

---

## Arithmetic Operations

All arithmetic operations return new matrices; they do not mutate the original.

### add(other)

Matrix addition.

**Parameters:**
- `other` - Matrix with matching dimensions

**Returns:** New Matrix = this + other

**Throws:** If dimensions don't match

```hemlock
import { from_rows } from "@stdlib/matrix";

let a = from_rows([[1, 2], [3, 4]]);
let b = from_rows([[5, 6], [7, 8]]);
let c = a.add(b);
print(c.get(0, 0));  // 6
print(c.get(1, 1));  // 12
```

### sub(other)

Matrix subtraction.

**Parameters:**
- `other` - Matrix with matching dimensions

**Returns:** New Matrix = this - other

**Throws:** If dimensions don't match

### mul(other)

Matrix multiplication.

**Parameters:**
- `other` - Matrix where this.cols == other.rows

**Returns:** New Matrix (this.rows x other.cols)

**Throws:** If dimensions are incompatible

```hemlock
import { from_rows } from "@stdlib/matrix";

let a = from_rows([[1, 2], [3, 4]]);
let b = from_rows([[5, 6], [7, 8]]);
let c = a.mul(b);
// [[1*5+2*7, 1*6+2*8], [3*5+4*7, 3*6+4*8]]
// [[19, 22], [43, 50]]
```

### scale(scalar)

Scalar multiplication.

**Parameters:**
- `scalar: f64` - Value to multiply each element by

**Returns:** New Matrix = this * scalar

```hemlock
import { from_rows } from "@stdlib/matrix";

let m = from_rows([[1, 2], [3, 4]]);
let scaled = m.scale(2.0);
print(scaled.get(0, 0));  // 2.0
print(scaled.get(1, 1));  // 8.0
```

### negate()

Negate all elements.

**Returns:** New Matrix = -this

### hadamard(other)

Element-wise multiplication (Hadamard product).

**Parameters:**
- `other` - Matrix with matching dimensions

**Returns:** New Matrix where each element = this[r,c] * other[r,c]

**Throws:** If dimensions don't match

---

## Linear Algebra

### transpose()

Returns the transpose of the matrix.

**Returns:** New Matrix with rows and columns swapped

```hemlock
import { from_rows } from "@stdlib/matrix";

let m = from_rows([[1, 2, 3], [4, 5, 6]]);
let t = m.transpose();
print(t.rows);      // 3
print(t.cols);      // 2
print(t.get(2, 0)); // 3
```

### trace()

Sum of diagonal elements. Requires a square matrix.

**Returns:** `f64` - Sum of diagonal elements

**Throws:** If matrix is not square

```hemlock
import { from_rows } from "@stdlib/matrix";

let m = from_rows([[1, 2], [3, 4]]);
print(m.trace());  // 5.0
```

### det()

Computes the determinant. Requires a square matrix. Uses optimized formulas for 1x1, 2x2, and 3x3 matrices, and cofactor expansion for larger sizes.

**Returns:** `f64` - Determinant

**Throws:** If matrix is not square

```hemlock
import { from_rows } from "@stdlib/matrix";

let m = from_rows([[1, 2], [3, 4]]);
print(m.det());  // -2.0
```

### inverse()

Computes the matrix inverse using Gauss-Jordan elimination with partial pivoting.

**Returns:** New Matrix = this^(-1)

**Throws:** If matrix is not square or is singular

```hemlock
import { from_rows, identity } from "@stdlib/matrix";

let m = from_rows([[1, 2], [3, 4]]);
let inv = m.inverse();
let product = m.mul(inv);
// product is approximately identity(2)
```

### norm()

Computes the Frobenius norm: sqrt(sum of squares of all elements).

**Returns:** `f64` - Frobenius norm

```hemlock
import { identity } from "@stdlib/matrix";

let I = identity(3);
print(I.norm());  // ~1.732 (sqrt(3))
```

### dot(other)

Element-wise dot product (sum of element-wise products). Matrices must have matching dimensions.

**Parameters:**
- `other` - Matrix with matching dimensions

**Returns:** `f64` - Dot product

**Throws:** If dimensions don't match

---

## Utility Methods

### map(f)

Apply a function to each element, returning a new matrix.

**Parameters:**
- `f` - Function taking (value, row, col) and returning the new value

**Returns:** New Matrix with transformed elements

```hemlock
import { from_rows } from "@stdlib/matrix";

let m = from_rows([[1, 2], [3, 4]]);
let doubled = m.map(fn(val, r, c) { return val * 2; });
print(doubled.get(0, 0));  // 2.0
```

### equals(other, epsilon?)

Check if two matrices are equal within a tolerance.

**Parameters:**
- `other` - Matrix to compare against
- `epsilon: f64` - Tolerance (default: 1e-10)

**Returns:** `bool` - true if all elements differ by at most epsilon

### submatrix(r1, c1, r2, c2)

Extract a submatrix from row r1 to r2 (exclusive) and col c1 to c2 (exclusive).

**Parameters:**
- `r1: i32` - Start row (inclusive)
- `c1: i32` - Start column (inclusive)
- `r2: i32` - End row (exclusive)
- `c2: i32` - End column (exclusive)

**Returns:** New Matrix containing the specified region

```hemlock
import { from_rows } from "@stdlib/matrix";

let m = from_rows([[1, 2, 3], [4, 5, 6], [7, 8, 9]]);
let sub = m.submatrix(0, 0, 2, 2);
// [[1, 2]
//  [4, 5]]
```

### to_string()

Returns a human-readable string representation.

**Returns:** `string` - Formatted matrix string

---

## Standalone Functions

### solve(A, b)

Solve a linear system Ax = b using Gaussian elimination with partial pivoting.

**Parameters:**
- `A` - Square coefficient matrix (NxN)
- `b` - Column vector (Nx1)

**Returns:** Column vector x (Nx1) such that Ax = b

**Throws:** If A is not square, b has wrong dimensions, or the system is singular

```hemlock
import { from_rows, col_vector, solve } from "@stdlib/matrix";

// Solve: 2x + y = 5, x + 3y = 10
let A = from_rows([[2, 1], [1, 3]]);
let b = col_vector([5, 10]);
let x = solve(A, b);
print(x.get(0, 0));  // 1.0
print(x.get(1, 0));  // 3.0
```

---

## Complete Example

```hemlock
import {
    Matrix, from_rows, identity,
    col_vector, solve, diagonal
} from "@stdlib/matrix";

// Create a 3x3 matrix
let A = from_rows([
    [2, 1, 0],
    [1, 3, 1],
    [0, 1, 2]
]);

// Basic properties
print("Trace: " + A.trace());
print("Det: " + A.det());

// Matrix arithmetic
let B = identity(3);
let C = A.add(B.scale(2.0));
print("A + 2I:");
print(C.to_string());

// Solve linear system Ax = b
let b = col_vector([4, 10, 6]);
let x = solve(A, b);
print("Solution x:");
print(x.to_string());

// Verify: A * x should equal b
let check = A.mul(x);
print("A * x:");
print(check.to_string());

// Inverse
let inv = A.inverse();
let product = A.mul(inv);
print("A * A^-1 is identity: " + product.equals(identity(3)));
```

---

## Implementation Notes

- All matrices store elements as `f64` internally
- Operations return new matrices (immutable style)
- Determinant uses cofactor expansion (efficient for small matrices, O(n!) for large)
- Inverse and solve use Gauss-Jordan elimination with partial pivoting for numerical stability
- Equality comparison uses a configurable epsilon tolerance (default 1e-10)
- Row-major storage order

---

## Error Handling

Matrix operations throw descriptive error messages:

```hemlock
import { Matrix, from_rows } from "@stdlib/matrix";

// Dimension mismatch
try {
    let a = Matrix(2, 3);
    let b = Matrix(3, 2);
    a.add(b);  // throws: dimensions must match
} catch (e) {
    print(e);
}

// Singular matrix
try {
    let singular = from_rows([[1, 2], [2, 4]]);
    singular.inverse();  // throws: singular
} catch (e) {
    print(e);
}

// Out of bounds
try {
    let m = Matrix(2, 2);
    m.get(5, 0);  // throws: index out of bounds
} catch (e) {
    print(e);
}
```

---

## Testing

Run the matrix module tests:

```bash
# Run all matrix tests
make test | grep stdlib_matrix

# Or run individual test
./hemlock tests/stdlib_matrix/test_basic.hml
```

---

## License

Part of the Hemlock standard library.
