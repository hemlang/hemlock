# @stdlib/iter - Iterator Utilities

The `iter` module provides common iteration patterns and array transformation utilities.

## Overview

- **Sequence Generation**: `range`, `frange`, `repeat`
- **Array Transformation**: `enumerate`, `zip`, `chunk`, `take`, `drop`
- **Array Processing**: `flatten`, `unique`, `group_by`, `partition`
- **Accumulation**: `sum`, `product`, `min_val`, `max_val`, `average`
- **Predicates**: `all`, `any`, `none`, `count`, `find_first`, `find_index`

## Usage

```hemlock
import { range, enumerate, zip, chunk } from "@stdlib/iter";
import { flatten, unique, sum, partition } from "@stdlib/iter";
import { all, any, find_first } from "@stdlib/iter";
```

---

## Sequence Generation

### range(end) / range(start, end, step?)

Generate a range of integers.

```hemlock
range(5);           // [0, 1, 2, 3, 4]
range(2, 5);        // [2, 3, 4]
range(0, 10, 2);    // [0, 2, 4, 6, 8]
range(5, 0, -1);    // [5, 4, 3, 2, 1]
```

### frange(start, end, step)

Generate a range of floats.

```hemlock
frange(0.0, 1.0, 0.2);  // [0.0, 0.2, 0.4, 0.6, 0.8]
```

### repeat(value, n)

Create an array with value repeated n times.

```hemlock
repeat(0, 5);       // [0, 0, 0, 0, 0]
repeat("x", 3);     // ["x", "x", "x"]
```

---

## Array Transformation

### enumerate(arr, start?)

Add index to each element.

```hemlock
enumerate(["a", "b", "c"]);     // [[0, "a"], [1, "b"], [2, "c"]]
enumerate(["a", "b"], 1);       // [[1, "a"], [2, "b"]]
```

### zip(a, b)

Combine two arrays into pairs.

```hemlock
zip([1, 2, 3], ["a", "b", "c"]);  // [[1, "a"], [2, "b"], [3, "c"]]
```

### zip3(a, b, c)

Combine three arrays into triples.

```hemlock
zip3([1, 2], ["a", "b"], [true, false]);
// [[1, "a", true], [2, "b", false]]
```

### unzip(pairs)

Split array of pairs into two arrays.

```hemlock
unzip([[1, "a"], [2, "b"]]);  // [[1, 2], ["a", "b"]]
```

### chunk(arr, size)

Split array into chunks.

```hemlock
chunk([1, 2, 3, 4, 5], 2);  // [[1, 2], [3, 4], [5]]
```

### take(arr, n)

Get first n elements.

```hemlock
take([1, 2, 3, 4, 5], 3);  // [1, 2, 3]
```

### drop(arr, n)

Skip first n elements.

```hemlock
drop([1, 2, 3, 4, 5], 2);  // [3, 4, 5]
```

### take_last(arr, n) / drop_last(arr, n)

Work from the end of the array.

```hemlock
take_last([1, 2, 3, 4, 5], 2);  // [4, 5]
drop_last([1, 2, 3, 4, 5], 2);  // [1, 2, 3]
```

---

## Array Processing

### flatten(arr)

Flatten one level of nesting.

```hemlock
flatten([[1, 2], [3, 4], [5]]);  // [1, 2, 3, 4, 5]
```

### flatten_deep(arr)

Flatten all levels of nesting.

```hemlock
flatten_deep([[1, [2, 3]], [[4], 5]]);  // [1, 2, 3, 4, 5]
```

### unique(arr)

Remove duplicates (preserves order).

```hemlock
unique([1, 2, 2, 3, 1, 4]);  // [1, 2, 3, 4]
```

### group_by(arr, key_fn)

Group elements by key function.

```hemlock
let people = [
    { name: "Alice", age: 30 },
    { name: "Bob", age: 25 },
    { name: "Carol", age: 30 }
];

group_by(people, fn(p) { return p.age; });
// { "30": [Alice, Carol], "25": [Bob] }
```

### partition(arr, pred)

Split into matching and non-matching.

```hemlock
partition([1, 2, 3, 4, 5], fn(x) { return x % 2 == 0; });
// [[2, 4], [1, 3, 5]]
```

---

## Accumulation

### sum(arr)

Sum all elements.

```hemlock
sum([1, 2, 3, 4, 5]);  // 15
```

### product(arr)

Multiply all elements.

```hemlock
product([1, 2, 3, 4]);  // 24
```

### min_val(arr) / max_val(arr)

Find minimum/maximum value.

```hemlock
min_val([3, 1, 4, 1, 5]);  // 1
max_val([3, 1, 4, 1, 5]);  // 5
```

### average(arr)

Calculate arithmetic mean.

```hemlock
average([1, 2, 3, 4, 5]);  // 3.0
```

---

## Predicates

### all(arr, pred)

Check if all elements match.

```hemlock
all([2, 4, 6], fn(x) { return x % 2 == 0; });  // true
all([2, 3, 6], fn(x) { return x % 2 == 0; });  // false
```

### any(arr, pred)

Check if any element matches.

```hemlock
any([1, 2, 3], fn(x) { return x > 2; });  // true
any([1, 2, 3], fn(x) { return x > 5; });  // false
```

### none(arr, pred)

Check if no elements match.

```hemlock
none([1, 3, 5], fn(x) { return x % 2 == 0; });  // true
```

### count(arr, pred)

Count matching elements.

```hemlock
count([1, 2, 3, 4, 5], fn(x) { return x % 2 == 0; });  // 2
```

### find_first(arr, pred)

Find first matching element (or null).

```hemlock
find_first([1, 2, 3, 4], fn(x) { return x > 2; });  // 3
find_first([1, 2, 3], fn(x) { return x > 5; });     // null
```

### find_index(arr, pred)

Find index of first match (or -1).

```hemlock
find_index([1, 2, 3, 4], fn(x) { return x > 2; });  // 2
find_index([1, 2, 3], fn(x) { return x > 5; });     // -1
```

---

## Examples

### Process CSV data

```hemlock
import { zip, enumerate } from "@stdlib/iter";

let headers = ["name", "age", "city"];
let row = ["Alice", "30", "NYC"];

let record = {};
let pairs = zip(headers, row);
let i = 0;
while (i < pairs.length) {
    record[pairs[i][0]] = pairs[i][1];
    i = i + 1;
}
// { name: "Alice", age: "30", city: "NYC" }
```

### Batch processing

```hemlock
import { chunk, range } from "@stdlib/iter";

let items = range(100);  // 0-99
let batches = chunk(items, 10);

// Process in batches of 10
let i = 0;
while (i < batches.length) {
    process_batch(batches[i]);
    i = i + 1;
}
```

### Data validation

```hemlock
import { all, any, partition } from "@stdlib/iter";

let scores = [85, 92, 78, 65, 88, 95];

// Check if all passing (>= 70)
let all_pass = all(scores, fn(s) { return s >= 70; });

// Check if any perfect (100)
let has_perfect = any(scores, fn(s) { return s == 100; });

// Split into pass/fail
let results = partition(scores, fn(s) { return s >= 70; });
let passing = results[0];
let failing = results[1];
```

---

## See Also

- Built-in `map`, `filter`, `reduce` methods on arrays
- `@stdlib/random` for shuffle and random sampling
- `@stdlib/collections` for data structures
