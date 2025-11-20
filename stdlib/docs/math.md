# Hemlock Math Module

A standard library module providing mathematical functions and constants for Hemlock programs.

## Overview

The math module provides comprehensive mathematical operations including:

- **Trigonometric functions** - sin, cos, tan, asin, acos, atan, atan2
- **Exponential & logarithmic functions** - sqrt, pow, exp, log, log10, log2
- **Rounding functions** - floor, ceil, round, trunc
- **Utility functions** - abs, min, max, clamp
- **Random number generation** - rand, rand_range, seed
- **Mathematical constants** - PI, E, TAU, INF, NAN

## Usage

```hemlock
import { sin, cos, PI } from "@stdlib/math";

let angle = PI / 4;
let x = cos(angle);
let y = sin(angle);
```

Or import all:

```hemlock
import * as math from "@stdlib/math";
let result = math.sqrt(16);
```

---

## Constants

### PI
The mathematical constant π (pi) ≈ 3.14159265358979323846

```hemlock
import { PI } from "@stdlib/math";
let circumference = 2.0 * PI * radius;
```

### E
The mathematical constant e (Euler's number) ≈ 2.71828182845904523536

```hemlock
import { E } from "@stdlib/math";
let growth = E * rate;
```

### TAU
The mathematical constant τ (tau) = 2π ≈ 6.28318530717958647692

```hemlock
import { TAU } from "@stdlib/math";
let full_circle = TAU;  // One complete rotation in radians
```

### INF
Positive infinity (IEEE 754 floating point infinity)

```hemlock
import { INF } from "@stdlib/math";
let max_value = INF;
```

### NAN
Not-a-Number (IEEE 754 floating point NaN)

```hemlock
import { NAN } from "@stdlib/math";
let undefined_result = NAN;
```

---

## Trigonometric Functions

All trigonometric functions work with radians.

### sin(x)
Returns the sine of x (x in radians).

**Parameters:**
- `x: number` - Angle in radians

**Returns:** `f64` - Sine of x in range [-1, 1]

```hemlock
import { sin, PI } from "@stdlib/math";

let result = sin(0.0);        // 0.0
let result2 = sin(PI / 2.0);  // 1.0
let result3 = sin(PI);        // ~0.0
```

### cos(x)
Returns the cosine of x (x in radians).

**Parameters:**
- `x: number` - Angle in radians

**Returns:** `f64` - Cosine of x in range [-1, 1]

```hemlock
import { cos, PI } from "@stdlib/math";

let result = cos(0.0);        // 1.0
let result2 = cos(PI / 2.0);  // ~0.0
let result3 = cos(PI);        // -1.0
```

### tan(x)
Returns the tangent of x (x in radians).

**Parameters:**
- `x: number` - Angle in radians

**Returns:** `f64` - Tangent of x

```hemlock
import { tan, PI } from "@stdlib/math";

let result = tan(0.0);        // 0.0
let result2 = tan(PI / 4.0);  // 1.0
```

### asin(x)
Returns the arc sine (inverse sine) of x in radians.

**Parameters:**
- `x: number` - Value in range [-1, 1]

**Returns:** `f64` - Arc sine in range [-π/2, π/2]

```hemlock
import { asin, PI } from "@stdlib/math";

let angle = asin(1.0);   // π/2
let angle2 = asin(0.5);  // π/6
```

### acos(x)
Returns the arc cosine (inverse cosine) of x in radians.

**Parameters:**
- `x: number` - Value in range [-1, 1]

**Returns:** `f64` - Arc cosine in range [0, π]

```hemlock
import { acos, PI } from "@stdlib/math";

let angle = acos(0.0);   // π/2
let angle2 = acos(1.0);  // 0.0
```

### atan(x)
Returns the arc tangent (inverse tangent) of x in radians.

**Parameters:**
- `x: number` - Any real number

**Returns:** `f64` - Arc tangent in range [-π/2, π/2]

```hemlock
import { atan } from "@stdlib/math";

let angle = atan(1.0);  // π/4
let angle2 = atan(0.0); // 0.0
```

### atan2(y, x)
Returns the arc tangent of y/x in radians, using the signs of both arguments to determine the quadrant.

**Parameters:**
- `y: number` - Y coordinate
- `x: number` - X coordinate

**Returns:** `f64` - Arc tangent in range [-π, π]

```hemlock
import { atan2, PI } from "@stdlib/math";

let angle = atan2(1.0, 1.0);    // π/4 (45 degrees)
let angle2 = atan2(1.0, -1.0);  // 3π/4 (135 degrees)
let angle3 = atan2(-1.0, -1.0); // -3π/4 (-135 degrees)
```

---

## Exponential & Logarithmic Functions

### sqrt(x)
Returns the square root of x.

**Parameters:**
- `x: number` - Non-negative number

**Returns:** `f64` - Square root of x

```hemlock
import { sqrt } from "@stdlib/math";

let result = sqrt(16.0);  // 4.0
let result2 = sqrt(2.0);  // 1.414213...
let result3 = sqrt(0.0);  // 0.0
```

### pow(base, exponent)
Returns base raised to the power of exponent.

**Parameters:**
- `base: number` - Base value
- `exponent: number` - Exponent value

**Returns:** `f64` - base^exponent

```hemlock
import { pow } from "@stdlib/math";

let result = pow(2.0, 3.0);   // 8.0
let result2 = pow(10.0, 2.0); // 100.0
let result3 = pow(4.0, 0.5);  // 2.0 (square root)
```

### exp(x)
Returns e raised to the power of x (e^x).

**Parameters:**
- `x: number` - Exponent

**Returns:** `f64` - e^x

```hemlock
import { exp } from "@stdlib/math";

let result = exp(0.0);  // 1.0
let result2 = exp(1.0); // 2.718281... (e)
let result3 = exp(2.0); // 7.389056...
```

### log(x)
Returns the natural logarithm (base e) of x.

**Parameters:**
- `x: number` - Positive number

**Returns:** `f64` - ln(x)

```hemlock
import { log, E } from "@stdlib/math";

let result = log(1.0);  // 0.0
let result2 = log(E);   // 1.0
let result3 = log(10.0); // 2.302585...
```

### log10(x)
Returns the base-10 logarithm of x.

**Parameters:**
- `x: number` - Positive number

**Returns:** `f64` - log₁₀(x)

```hemlock
import { log10 } from "@stdlib/math";

let result = log10(1.0);    // 0.0
let result2 = log10(10.0);  // 1.0
let result3 = log10(100.0); // 2.0
```

### log2(x)
Returns the base-2 logarithm of x.

**Parameters:**
- `x: number` - Positive number

**Returns:** `f64` - log₂(x)

```hemlock
import { log2 } from "@stdlib/math";

let result = log2(1.0);  // 0.0
let result2 = log2(2.0); // 1.0
let result3 = log2(8.0); // 3.0
```

---

## Rounding Functions

### floor(x)
Returns the largest integer less than or equal to x.

**Parameters:**
- `x: number` - Any number

**Returns:** `f64` - Floor of x

```hemlock
import { floor } from "@stdlib/math";

let result = floor(3.7);   // 3.0
let result2 = floor(-2.3); // -3.0
let result3 = floor(5.0);  // 5.0
```

### ceil(x)
Returns the smallest integer greater than or equal to x.

**Parameters:**
- `x: number` - Any number

**Returns:** `f64` - Ceiling of x

```hemlock
import { ceil } from "@stdlib/math";

let result = ceil(3.2);   // 4.0
let result2 = ceil(-2.7); // -2.0
let result3 = ceil(5.0);  // 5.0
```

### round(x)
Returns x rounded to the nearest integer. Halfway cases round away from zero.

**Parameters:**
- `x: number` - Any number

**Returns:** `f64` - Rounded value

```hemlock
import { round } from "@stdlib/math";

let result = round(3.5);   // 4.0
let result2 = round(3.4);  // 3.0
let result3 = round(-2.5); // -3.0
```

### trunc(x)
Returns the integer part of x, removing any fractional digits (rounds toward zero).

**Parameters:**
- `x: number` - Any number

**Returns:** `f64` - Truncated value

```hemlock
import { trunc } from "@stdlib/math";

let result = trunc(3.7);   // 3.0
let result2 = trunc(-2.9); // -2.0
let result3 = trunc(5.0);  // 5.0
```

---

## Utility Functions

### abs(x)
Returns the absolute value of x.

**Parameters:**
- `x: number` - Any number

**Returns:** `f64` - |x|

```hemlock
import { abs } from "@stdlib/math";

let result = abs(-5.0);  // 5.0
let result2 = abs(3.2);  // 3.2
let result3 = abs(0.0);  // 0.0
```

### min(a, b)
Returns the smaller of two values.

**Parameters:**
- `a: number` - First value
- `b: number` - Second value

**Returns:** `f64` - Minimum of a and b

```hemlock
import { min } from "@stdlib/math";

let result = min(5.0, 10.0);  // 5.0
let result2 = min(-3.0, 2.0); // -3.0
```

### max(a, b)
Returns the larger of two values.

**Parameters:**
- `a: number` - First value
- `b: number` - Second value

**Returns:** `f64` - Maximum of a and b

```hemlock
import { max } from "@stdlib/math";

let result = max(5.0, 10.0);  // 10.0
let result2 = max(-3.0, 2.0); // 2.0
```

### clamp(value, min_val, max_val)
Constrains a value to lie within a range [min, max].

**Parameters:**
- `value: number` - Value to clamp
- `min_val: number` - Minimum bound
- `max_val: number` - Maximum bound

**Returns:** `f64` - Clamped value

```hemlock
import { clamp } from "@stdlib/math";

let result = clamp(5.0, 0.0, 10.0);   // 5.0
let result2 = clamp(-3.0, 0.0, 10.0); // 0.0 (clamped to min)
let result3 = clamp(15.0, 0.0, 10.0); // 10.0 (clamped to max)
```

---

## Random Number Generation

### rand()
Returns a random floating-point number in the range [0.0, 1.0).

**Parameters:** None

**Returns:** `f64` - Random value in [0.0, 1.0)

```hemlock
import { rand } from "@stdlib/math";

let random = rand();  // e.g., 0.7382491...
let random2 = rand(); // e.g., 0.2194837...
```

### rand_range(min_val, max_val)
Returns a random floating-point number in the range [min, max).

**Parameters:**
- `min_val: number` - Minimum value (inclusive)
- `max_val: number` - Maximum value (exclusive)

**Returns:** `f64` - Random value in [min, max)

```hemlock
import { rand_range } from "@stdlib/math";

let dice = rand_range(1.0, 7.0);       // Random 1.0 to 6.999...
let temperature = rand_range(20.0, 30.0); // Random 20-30°C
```

### seed(value)
Seeds the random number generator with a specific value for reproducible sequences.

**Parameters:**
- `value: i32` - Seed value

**Returns:** `null`

```hemlock
import { seed, rand } from "@stdlib/math";

seed(42);
let r1 = rand();  // Same sequence every time with seed 42
let r2 = rand();

seed(42);         // Reset to same seed
let r3 = rand();  // r3 == r1
```

---

## Complete Example

```hemlock
import {
    sin, cos, tan, sqrt, pow,
    PI, abs, min, max,
    rand_range, seed
} from "@stdlib/math";

// Trigonometry: Calculate point on unit circle
fn point_on_circle(angle: f64): object {
    return {
        x: cos(angle),
        y: sin(angle)
    };
}

let p = point_on_circle(PI / 4.0);
print("Point at 45°: (" + typeof(p.x) + ", " + typeof(p.y) + ")");

// Distance formula
fn distance(x1: f64, y1: f64, x2: f64, y2: f64): f64 {
    let dx = x2 - x1;
    let dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

let dist = distance(0.0, 0.0, 3.0, 4.0);
print("Distance: " + typeof(dist));  // 5.0

// Random dice roller
fn roll_dice(sides: i32): i32 {
    let result: i32 = rand_range(1.0, sides + 1.0);
    return result;
}

seed(42);  // Reproducible results
let roll = roll_dice(6);
print("Dice roll: " + typeof(roll));

// Find max of three values
fn max3(a: f64, b: f64, c: f64): f64 {
    return max(max(a, b), c);
}

print("Max: " + typeof(max3(5.0, 12.0, 8.0)));  // 12.0
```

---

## Implementation Notes

- All functions wrap C standard library `<math.h>` functions
- All functions return `f64` (64-bit floating point)
- Trigonometric functions use **radians**, not degrees
- To convert degrees to radians: `radians = degrees * PI / 180.0`
- To convert radians to degrees: `degrees = radians * 180.0 / PI`
- Random number generator uses C `rand()` (not cryptographically secure)
- `seed()` uses C `srand()` for seeding

---

## Error Handling

Math functions follow C standard behavior:
- `sqrt(-1.0)` returns `NAN`
- `log(0.0)` returns `-INF`
- `log(-1.0)` returns `NAN`
- Division by zero in calculations returns `INF` or `NAN`
- No exceptions are thrown for domain errors

```hemlock
import { sqrt, log, NAN, INF } from "@stdlib/math";

let result1 = sqrt(-1.0);  // NAN
let result2 = log(0.0);    // -INF
let result3 = log(-1.0);   // NAN

// Check for invalid results
if (result1 != result1) {  // NAN != NAN is true
    print("Result is NaN");
}
```

---

## Testing

Run the math module tests:

```bash
# Run all math tests
make test | grep stdlib_math

# Or run individual test
./hemlock tests/stdlib_math/test_math_constants.hml
```

---

## License

Part of the Hemlock standard library.
