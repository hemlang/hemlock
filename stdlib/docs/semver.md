# @stdlib/semver - Semantic Versioning

Provides parsing, comparison, and manipulation of semantic versions following the SemVer 2.0.0 specification.

## Import

```hemlock
import { parse, compare, satisfies, increment } from "@stdlib/semver";
import { lt, gt, eq, lte, gte, neq } from "@stdlib/semver";
import { valid, major, minor, patch, sort } from "@stdlib/semver";
```

## Parsing and Formatting

### parse(version: string): object

Parse a semantic version string.

```hemlock
let v = parse("1.2.3-beta.1+build.456");
print(v["major"]);      // 1
print(v["minor"]);      // 2
print(v["patch"]);      // 3
print(v["prerelease"]); // "beta.1"
print(v["build"]);      // "build.456"

// Leading 'v' is optional
parse("v2.0.0");  // Works
```

### format(ver: object): string

Convert a version object back to string.

```hemlock
let v = parse("1.2.3-alpha");
print(format(v));  // "1.2.3-alpha"
```

## Comparison Functions

### compare(a, b): i32

Compare two versions. Returns -1, 0, or 1.

```hemlock
compare("1.0.0", "2.0.0");  // -1 (a < b)
compare("2.0.0", "1.0.0");  // 1  (a > b)
compare("1.0.0", "1.0.0");  // 0  (a == b)
```

### Comparison Operators

```hemlock
lt("1.0.0", "2.0.0");   // true  (less than)
lte("1.0.0", "1.0.0");  // true  (less than or equal)
gt("2.0.0", "1.0.0");   // true  (greater than)
gte("1.0.0", "1.0.0");  // true  (greater than or equal)
eq("1.0.0", "1.0.0");   // true  (equal)
neq("1.0.0", "2.0.0");  // true  (not equal)
```

### Prerelease Comparison

Follows SemVer precedence rules:

```hemlock
// Release > prerelease
compare("1.0.0", "1.0.0-alpha");        // 1

// Alphabetic comparison
compare("1.0.0-alpha", "1.0.0-beta");   // -1

// Numeric identifiers compared as numbers
compare("1.0.0-alpha.1", "1.0.0-alpha.2");   // -1
compare("1.0.0-alpha.10", "1.0.0-alpha.2");  // 1

// Longer prerelease > shorter
compare("1.0.0-alpha.1", "1.0.0-alpha.1.0"); // -1
```

## Version Incrementing

### increment(version, release, identifier?): string

Increment a version by release type.

```hemlock
increment("1.2.3", "major");      // "2.0.0"
increment("1.2.3", "minor");      // "1.3.0"
increment("1.2.3", "patch");      // "1.2.4"

// Prerelease increments
increment("1.2.3", "prerelease", "alpha");  // "1.2.4-alpha"
increment("1.2.4-alpha", "prerelease");     // "1.2.4-alpha.0"
increment("1.2.4-alpha.0", "prerelease");   // "1.2.4-alpha.1"

// Pre-versions
increment("1.2.3", "premajor", "rc");   // "2.0.0-rc"
increment("1.2.3", "preminor", "rc");   // "1.3.0-rc"
increment("1.2.3", "prepatch", "rc");   // "1.2.4-rc"
```

## Range Matching

### satisfies(version, range): bool

Check if a version satisfies a range specification.

```hemlock
// Comparison operators
satisfies("1.2.3", ">=1.0.0");        // true
satisfies("1.2.3", "<2.0.0");         // true
satisfies("1.2.3", "=1.2.3");         // true
satisfies("1.2.3", "!=1.0.0");        // true

// Combined ranges (AND - space separated)
satisfies("1.2.3", ">=1.0.0 <2.0.0"); // true

// OR ranges (|| separated)
satisfies("3.0.0", "^1.0.0 || ^3.0.0"); // true
```

### Caret Ranges (^)

Allows changes that don't modify the left-most non-zero digit.

```hemlock
// ^1.2.3 means >=1.2.3 <2.0.0
satisfies("1.5.0", "^1.2.3");  // true
satisfies("2.0.0", "^1.2.3");  // false

// ^0.2.3 means >=0.2.3 <0.3.0
satisfies("0.2.5", "^0.2.3");  // true
satisfies("0.3.0", "^0.2.3");  // false

// ^0.0.3 means >=0.0.3 <0.0.4
satisfies("0.0.3", "^0.0.3");  // true
satisfies("0.0.4", "^0.0.3");  // false
```

### Tilde Ranges (~)

Allows patch-level changes.

```hemlock
// ~1.2.3 means >=1.2.3 <1.3.0
satisfies("1.2.5", "~1.2.3");  // true
satisfies("1.3.0", "~1.2.3");  // false
```

## Utility Functions

### Version Part Extraction

```hemlock
major("1.2.3");      // 1
minor("1.2.3");      // 2
patch("1.2.3");      // 3
prerelease("1.0.0-alpha.1");  // "alpha.1"
```

### Validation

```hemlock
valid("1.2.3");       // true
valid("1.2");         // true (partial versions ok)
valid("invalid");     // false
```

### Cleaning

```hemlock
clean("v1.2.3");            // "1.2.3"
clean("1.2.3-beta+build");  // "1.2.3-beta+build"
```

### Min/Max

```hemlock
max("1.0.0", "2.0.0");  // "2.0.0"
min("1.0.0", "2.0.0");  // "1.0.0"
```

### Sorting

```hemlock
let versions = ["2.0.0", "1.0.0", "1.5.0", "1.0.1"];

sort(versions);   // ["1.0.0", "1.0.1", "1.5.0", "2.0.0"]
rsort(versions);  // ["2.0.0", "1.5.0", "1.0.1", "1.0.0"]
```

### Range Satisfaction

```hemlock
let versions = ["1.0.0", "1.2.0", "1.5.0", "2.0.0"];

max_satisfying(versions, "^1.0.0");  // "1.5.0"
min_satisfying(versions, "^1.0.0");  // "1.0.0"
```

### Difference

```hemlock
diff("1.0.0", "2.0.0");          // "major"
diff("1.0.0", "1.1.0");          // "minor"
diff("1.0.0", "1.0.1");          // "patch"
diff("1.0.0", "1.0.0-alpha");    // "prerelease"
diff("1.0.0", "1.0.0");          // null
```

## Examples

### Dependency Version Checking

```hemlock
import { satisfies, max_satisfying } from "@stdlib/semver";

let required = "^2.0.0";
let available = ["1.5.0", "2.0.0", "2.1.0", "2.5.0", "3.0.0"];

let best = max_satisfying(available, required);
if (best != null) {
    print("Using version: " + best);  // "2.5.0"
} else {
    print("No compatible version found");
}
```

### Version Bumping Script

```hemlock
import { parse, increment, format } from "@stdlib/semver";

fn bump_version(current: string, change_type: string): string {
    return increment(current, change_type);
}

let version = "1.2.3";
print("Current: " + version);
print("After patch: " + bump_version(version, "patch"));
print("After minor: " + bump_version(version, "minor"));
print("After major: " + bump_version(version, "major"));
```

### Sorting Releases

```hemlock
import { rsort } from "@stdlib/semver";

let releases = [
    "1.0.0",
    "1.0.0-alpha",
    "1.0.0-beta",
    "1.0.0-rc.1",
    "1.0.0-rc.2",
    "0.9.0"
];

let sorted = rsort(releases);
print("Latest: " + sorted[0]);  // "1.0.0"
```

## SemVer Specification

This module follows the Semantic Versioning 2.0.0 specification:

- Format: `MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]`
- MAJOR: Incremented for incompatible API changes
- MINOR: Incremented for backwards-compatible features
- PATCH: Incremented for backwards-compatible bug fixes
- PRERELEASE: Optional, indicates unstable version
- BUILD: Optional metadata, ignored in comparisons

See: https://semver.org/
