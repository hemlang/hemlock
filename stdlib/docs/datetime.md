# Hemlock Standard Library: Date & Time (`@stdlib/datetime`)

The datetime module provides comprehensive date and time manipulation functionality, including formatting, parsing, arithmetic, and comparison operations.

## Overview

The datetime module builds on the `@stdlib/time` module's Unix timestamp functions to provide a high-level DateTime API for working with dates and times. It supports:

- **Date/time creation** from timestamps, components, or current time
- **Formatting** with strftime-style format strings
- **Parsing** ISO 8601 date strings
- **Arithmetic** operations (add days, hours, minutes, seconds)
- **Comparison** operations (is_before, is_after, is_equal)
- **Difference calculation** between dates

## Quick Start

```hemlock
import { now, from_date, parse_iso } from "@stdlib/datetime";

// Get current date/time
let current = now();
print(current.to_string());  // "2025-01-16 12:30:45"

// Create from specific date
let birthday = from_date(1990, 5, 15, 14, 30, 0);
print(birthday.format("%B %d, %Y"));  // "May 15, 1990"

// Parse ISO date
let meeting = parse_iso("2025-03-20T14:30:00");
print(meeting.to_string());  // "2025-03-20 14:30:00"

// Date arithmetic
let next_week = current.add_days(7);
print("Next week: " + next_week.to_date_string());

// Comparison
if (meeting.is_after(current)) {
    print("Meeting is in the future");
}
```

## API Reference

### DateTime Constructor

#### `DateTime(timestamp?: i64): object`

Create a DateTime object from a Unix timestamp (seconds since epoch). If no timestamp is provided, uses the current time.

```hemlock
import { DateTime } from "@stdlib/datetime";

// From specific timestamp
let dt = DateTime(1737037845);
print(dt.to_string());  // "2025-01-16 12:30:45"

// Current time (default)
let now_dt = DateTime();
print(now_dt.year);  // Current year
```

**Returns:** DateTime object with the following fields:
- `timestamp: i64` - Unix timestamp
- `year: i32` - Full year (e.g., 2025)
- `month: i32` - Month (1-12)
- `day: i32` - Day of month (1-31)
- `hour: i32` - Hour (0-23)
- `minute: i32` - Minute (0-59)
- `second: i32` - Second (0-59)
- `weekday: i32` - Day of week (0=Sunday, 6=Saturday)
- `yearday: i32` - Day of year (1-366)
- `isdst: bool` - Whether daylight saving time is in effect

---

### Convenience Constructors

#### `now(): object`

Create a DateTime object for the current time.

```hemlock
import { now } from "@stdlib/datetime";

let current = now();
print("Current time: " + current.to_string());
```

#### `from_date(year: i32, month: i32, day: i32, hour?: i32, minute?: i32, second?: i32): object`

Create a DateTime object from date/time components (local time).

```hemlock
import { from_date } from "@stdlib/datetime";

// Date only (time defaults to 00:00:00)
let date = from_date(2025, 12, 25);
print(date.to_string());  // "2025-12-25 00:00:00"

// Date and time
let datetime = from_date(2025, 3, 15, 14, 30, 45);
print(datetime.to_string());  // "2025-03-15 14:30:45"
```

**Parameters:**
- `year` - Full year (e.g., 2025)
- `month` - Month (1-12)
- `day` - Day of month (1-31)
- `hour` - Hour (0-23), defaults to 0
- `minute` - Minute (0-59), defaults to 0
- `second` - Second (0-59), defaults to 0

#### `from_utc(year: i32, month: i32, day: i32, hour?: i32, minute?: i32, second?: i32): object`

Create a DateTime object from UTC date/time components.

```hemlock
import { from_utc } from "@stdlib/datetime";

let utc_dt = from_utc(2025, 1, 1, 0, 0, 0);
print(utc_dt.to_string());
```

---

### Formatting Methods

#### `.format(fmt: string): string`

Format the date/time using strftime-style format string.

```hemlock
let dt = from_date(2025, 3, 15, 14, 30, 45);

print(dt.format("%Y-%m-%d"));           // "2025-03-15"
print(dt.format("%B %d, %Y"));          // "March 15, 2025"
print(dt.format("%Y-%m-%d %H:%M:%S"));  // "2025-03-15 14:30:45"
print(dt.format("%I:%M %p"));           // "02:30 PM"
```

**Supported format codes:**
- `%Y` - 4-digit year (e.g., 2025)
- `%y` - 2-digit year (e.g., 25)
- `%m` - Month as number (01-12)
- `%B` - Full month name (e.g., January)
- `%b` - Abbreviated month name (e.g., Jan)
- `%d` - Day of month (01-31)
- `%H` - Hour (00-23)
- `%I` - Hour (01-12)
- `%M` - Minute (00-59)
- `%S` - Second (00-59)
- `%p` - AM/PM
- `%A` - Full weekday name (e.g., Monday)
- `%a` - Abbreviated weekday name (e.g., Mon)
- `%w` - Weekday as number (0-6, 0=Sunday)
- `%j` - Day of year (001-366)
- `%%` - Literal %

#### `.to_string(): string`

Format as "YYYY-MM-DD HH:MM:SS".

```hemlock
let dt = from_date(2025, 3, 15, 14, 30, 45);
print(dt.to_string());  // "2025-03-15 14:30:45"
```

#### `.to_date_string(): string`

Format as "YYYY-MM-DD".

```hemlock
let dt = from_date(2025, 3, 15, 14, 30, 45);
print(dt.to_date_string());  // "2025-03-15"
```

#### `.to_time_string(): string`

Format as "HH:MM:SS".

```hemlock
let dt = from_date(2025, 3, 15, 14, 30, 45);
print(dt.to_time_string());  // "14:30:45"
```

#### `.to_iso_string(): string`

Format as ISO 8601 string in UTC: "YYYY-MM-DDTHH:MM:SSZ".

```hemlock
let dt = from_date(2025, 3, 15, 14, 30, 45);
print(dt.to_iso_string());  // "2025-03-15T14:30:45Z" (UTC)
```

#### `.weekday_name(): string`

Get the full weekday name.

```hemlock
let dt = from_date(2025, 3, 15, 0, 0, 0);  // Saturday
print(dt.weekday_name());  // "Saturday"
```

#### `.month_name(): string`

Get the full month name.

```hemlock
let dt = from_date(2025, 3, 15, 0, 0, 0);
print(dt.month_name());  // "March"
```

---

### Parsing Functions

#### `parse_iso(date_str: string): object`

Parse an ISO 8601 date string into a DateTime object.

Supported formats:
- `YYYY-MM-DD` - Date only
- `YYYY-MM-DDTHH:MM:SS` - Date and time
- `YYYY-MM-DDTHH:MM:SSZ` - Date and time (UTC)

```hemlock
import { parse_iso } from "@stdlib/datetime";

// Date only
let dt1 = parse_iso("2025-01-15");
print(dt1.to_string());  // "2025-01-15 00:00:00"

// Date and time
let dt2 = parse_iso("2025-03-20T14:30:45");
print(dt2.to_string());  // "2025-03-20 14:30:45"

// UTC format
let dt3 = parse_iso("2025-12-31T23:59:59Z");
print(dt3.to_string());
```

---

### Date Arithmetic Methods

#### `.add_days(days: i32): object`

Add (or subtract) days from the date. Returns a new DateTime object.

```hemlock
let dt = from_date(2025, 1, 15, 12, 0, 0);

let future = dt.add_days(7);
print(future.to_string());  // "2025-01-22 12:00:00"

let past = dt.add_days(-3);
print(past.to_string());    // "2025-01-12 12:00:00"
```

#### `.add_hours(hours: i32): object`

Add (or subtract) hours. Returns a new DateTime object.

```hemlock
let dt = from_date(2025, 1, 15, 12, 0, 0);

let later = dt.add_hours(6);
print(later.to_string());  // "2025-01-15 18:00:00"

let earlier = dt.add_hours(-2);
print(earlier.to_string());  // "2025-01-15 10:00:00"
```

#### `.add_minutes(minutes: i32): object`

Add (or subtract) minutes. Returns a new DateTime object.

```hemlock
let dt = from_date(2025, 1, 15, 12, 0, 0);

let later = dt.add_minutes(45);
print(later.to_string());  // "2025-01-15 12:45:00"
```

#### `.add_seconds(seconds: i32): object`

Add (or subtract) seconds. Returns a new DateTime object.

```hemlock
let dt = from_date(2025, 1, 15, 12, 0, 0);

let later = dt.add_seconds(30);
print(later.to_string());  // "2025-01-15 12:00:30"
```

---

### Difference Methods

Calculate the difference between two DateTime objects.

#### `.diff_days(other: object): i64`

Get the difference in days.

```hemlock
let dt1 = from_date(2025, 1, 1, 0, 0, 0);
let dt2 = from_date(2025, 1, 11, 0, 0, 0);

let diff = dt2.diff_days(dt1);
print(diff);  // 10
```

#### `.diff_hours(other: object): i64`

Get the difference in hours.

```hemlock
let dt1 = from_date(2025, 1, 1, 10, 0, 0);
let dt2 = from_date(2025, 1, 1, 16, 0, 0);

let diff = dt2.diff_hours(dt1);
print(diff);  // 6
```

#### `.diff_minutes(other: object): i64`

Get the difference in minutes.

```hemlock
let dt1 = from_date(2025, 1, 1, 12, 0, 0);
let dt2 = from_date(2025, 1, 1, 12, 30, 0);

let diff = dt2.diff_minutes(dt1);
print(diff);  // 30
```

#### `.diff_seconds(other: object): i64`

Get the difference in seconds.

```hemlock
let dt1 = from_date(2025, 1, 1, 12, 0, 0);
let dt2 = from_date(2025, 1, 1, 12, 0, 45);

let diff = dt2.diff_seconds(dt1);
print(diff);  // 45
```

---

### Comparison Methods

#### `.is_before(other: object): bool`

Check if this DateTime is before another.

```hemlock
let dt1 = from_date(2025, 1, 15, 0, 0, 0);
let dt2 = from_date(2025, 1, 20, 0, 0, 0);

if (dt1.is_before(dt2)) {
    print("dt1 is before dt2");  // This will print
}
```

#### `.is_after(other: object): bool`

Check if this DateTime is after another.

```hemlock
let dt1 = from_date(2025, 1, 20, 0, 0, 0);
let dt2 = from_date(2025, 1, 15, 0, 0, 0);

if (dt1.is_after(dt2)) {
    print("dt1 is after dt2");  // This will print
}
```

#### `.is_equal(other: object): bool`

Check if two DateTime objects represent the same moment in time.

```hemlock
let dt1 = from_date(2025, 1, 15, 12, 0, 0);
let dt2 = from_date(2025, 1, 15, 12, 0, 0);

if (dt1.is_equal(dt2)) {
    print("Same time");  // This will print
}
```

---

### Low-Level Builtins

These functions are exposed for advanced use cases but are typically not needed when using the DateTime API.

#### `localtime(timestamp: i64): object`

Convert Unix timestamp to local time components.

```hemlock
import { localtime } from "@stdlib/datetime";

let components = localtime(1737037845);
print("Year: " + typeof(components.year));
print("Month: " + typeof(components.month));
print("Day: " + typeof(components.day));
```

**Returns:** Object with fields: `year`, `month`, `day`, `hour`, `minute`, `second`, `weekday`, `yearday`, `isdst`

#### `gmtime(timestamp: i64): object`

Convert Unix timestamp to UTC time components.

```hemlock
import { gmtime } from "@stdlib/datetime";

let utc_components = gmtime(1737037845);
print("UTC hour: " + typeof(utc_components.hour));
```

#### `mktime(components: object): i64`

Convert time components to Unix timestamp.

```hemlock
import { mktime } from "@stdlib/datetime";

let components = {
    year: 2025,
    month: 1,
    day: 15,
    hour: 12,
    minute: 30,
    second: 45
};

let timestamp = mktime(components);
print("Timestamp: " + typeof(timestamp));
```

**Required fields:** `year`, `month`, `day`
**Optional fields:** `hour`, `minute`, `second` (default to 0)

#### `strftime(format: string, components: object): string`

Format time components using strftime format string.

```hemlock
import { strftime } from "@stdlib/datetime";

let components = {
    year: 2025,
    month: 12,
    day: 25,
    hour: 0,
    minute: 0,
    second: 0,
    weekday: 0,
    yearday: 1
};

let formatted = strftime("%B %d, %Y", components);
print(formatted);  // "December 25, 2025"
```

---

## Complete Examples

### Birthday Calculator

```hemlock
import { from_date, now } from "@stdlib/datetime";

let birthday = from_date(1990, 5, 15, 0, 0, 0);
let today = now();

let days_old = today.diff_days(birthday);
let years_old = days_old / 365;

print("Born on: " + birthday.format("%B %d, %Y"));
print("Age: ~" + typeof(years_old) + " years");
print("Days old: " + typeof(days_old));
```

### Event Countdown

```hemlock
import { from_date, now } from "@stdlib/datetime";

let event = from_date(2025, 12, 25, 0, 0, 0);
let current = now();

if (event.is_after(current)) {
    let days_until = event.diff_days(current);
    print("Days until event: " + typeof(days_until));
} else {
    print("Event has already occurred");
}
```

### Meeting Scheduler

```hemlock
import { now, parse_iso } from "@stdlib/datetime";

// Parse meeting times from ISO strings
let meeting1 = parse_iso("2025-03-20T14:30:00");
let meeting2 = parse_iso("2025-03-20T16:00:00");

// Calculate duration
let duration_minutes = meeting2.diff_minutes(meeting1);
print("Meeting duration: " + typeof(duration_minutes) + " minutes");

// Check if meeting is in the future
let current = now();
if (meeting1.is_after(current)) {
    let days_until = meeting1.diff_days(current);
    print("Meeting in " + typeof(days_until) + " days");
    print("Meeting time: " + meeting1.format("%A, %B %d at %I:%M %p"));
}
```

### Date Range Iteration

```hemlock
import { from_date } from "@stdlib/datetime";

let start = from_date(2025, 1, 1, 0, 0, 0);
let end = from_date(2025, 1, 7, 0, 0, 0);

let current = start;
while (current.is_before(end) || current.is_equal(end)) {
    print(current.format("%A, %B %d, %Y"));
    current = current.add_days(1);
}
```

---

## Implementation Notes

### Time Zones
- DateTime objects store local time by default
- Use `to_iso_string()` for UTC representation
- Full timezone support is planned for a future version

### Leap Years & DST
- The underlying C library (libc) handles leap years and daylight saving time
- DST transitions are automatically handled by the system

### Precision
- Timestamps are stored as 64-bit integers (seconds since epoch)
- Sub-second precision is not currently supported
- Date arithmetic operates on whole seconds

### Performance
- DateTime objects are lightweight (stored as objects with i32/i64 fields)
- Formatting operations are efficient (use C's strftime)
- Date arithmetic is O(1) (simple timestamp math)

---

## See Also

- **@stdlib/time** - Lower-level time functions (now, sleep, clock)
- **strftime(3)** - POSIX strftime manual page for format codes
- **ISO 8601** - International date/time format standard

---

## Testing

Comprehensive tests are available in `tests/stdlib_datetime/`:
- `test_basic.hml` - DateTime creation and basic operations
- `test_arithmetic.hml` - Date arithmetic (add days/hours/etc.)
- `test_comparison.hml` - Comparison operations
- `test_formatting.hml` - Format string testing
- `test_parsing.hml` - ISO 8601 parsing

Run tests:
```bash
./hemlock tests/stdlib_datetime/test_basic.hml
./hemlock tests/stdlib_datetime/test_arithmetic.hml
./hemlock tests/stdlib_datetime/test_comparison.hml
./hemlock tests/stdlib_datetime/test_formatting.hml
./hemlock tests/stdlib_datetime/test_parsing.hml
```
