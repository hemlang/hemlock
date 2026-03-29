# Hemlock Language Reference (Compact)

Systems scripting language. Manual memory, no GC, C-like syntax, semicolons mandatory.

## Types
Signed: i8, i16, i32, i64 | Unsigned: u8, u16, u32, u64 | Floats: f32, f64
Other: bool, string, rune, array, ptr, buffer, null, object, file, task, channel
Aliases: integer=i32, number=f64, byte=u8

Literals: `42` → i32, `5000000000` → i64, `3.14` → f64, `0xFF` hex, `0b1010` bin, `0o777` oct
Type conversion: `i32("42")`, `f64(100)`, `i64(42)`
Introspection: `typeof(x)`, `"hello".length` (runes), `"hello".byte_length` (bytes), `[1,2].length`

## Variables & Control Flow
```
let x = 42;
let x: i32 = 42;
if (cond) { } else if (cond) { } else { }
while (cond) { break; continue; }
for (let i = 0; i < 10; i++) { }
for (item in array) { }
loop { if (done) { break; } }
switch (x) { case 1: break; default: break; }  // C-style fall-through
defer cleanup();
```

Loop labels: `outer: while (c) { inner: for (...) { break outer; } }`

## Operators
Arithmetic: `+`, `-`, `*`, `/` (ALWAYS returns float! use `divi()` for integer division), `%`
Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
Logical: `&&`, `||`, `!`
Bitwise: `&`, `|`, `^`, `~`, `<<`, `>>`
Increment: `++x`, `x++`, `--x`, `x--`
Compound: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
Null: `??` (coalesce), `??=` (coalesce assign), `?.` (safe navigation)

## Functions
```
fn add(a: i32, b: i32): i32 { return a + b; }
fn greet(name: string, msg?: "Hello") { print(msg + " " + name); }
let f = fn(x) { return x * 2; };
fn double(x: i32): i32 => x * 2;  // expression-bodied
fn swap(ref a: i32, ref b: i32) { let t = a; a = b; b = t; }  // pass-by-reference
fn readonly(const items: array) { }  // immutable param
create_user(name: "Bob", age: 30);  // named arguments
```

## Pattern Matching
```
let result = match (value) {
    0 => "zero",
    1 | 2 | 3 => "small",
    n if n < 10 => "medium",
    n => "large: " + n
};
// Also: type patterns (n: i32), object destructuring ({ x, y }), array destructuring ([first, ...rest]), wildcard (_)
```

## Objects & Enums
```
define Person { name: string, age: i32, active?: true }
let p: Person = { name: "Alice", age: 30 };
let person = { name, age };  // shorthand
let config = { ...defaults, size: "large" };  // spread
enum Color { RED, GREEN, BLUE }
// Compound types: let p: HasName & HasAge = { name: "A", age: 1 };
// Type aliases: type Person = HasName & HasAge;
```

## Strings (19 methods, mutable via index)
`substr`, `slice`, `find`, `contains`, `split`, `trim`, `to_upper`, `to_lower`,
`starts_with`, `ends_with`, `replace`, `replace_all`, `repeat`, `char_at`,
`byte_at`, `chars`, `bytes`, `to_bytes`, `deserialize`
Template strings: `` `Hello ${name}!` ``

## Arrays (23 methods)
`push`, `pop`, `shift`, `unshift`, `insert`, `remove`, `find`, `contains`,
`slice`, `join`, `concat`, `reverse`, `first`, `last`, `clear`, `map`, `filter`, `reduce`,
`every`, `some`, `indexOf`, `sort`, `fill`
Typed: `let nums: array<i32> = [1, 2, 3];`

## Memory
```
let p = alloc(64);       // raw pointer
let b = buffer(64);      // bounds-checked
memset(p, 0, 64); memcpy(dest, src, 64);
free(p);                 // manual cleanup
ptr_write_i32(p, value); ptr_read_i32(p);  // pointer helpers
ptr_write_u8(p, value); ptr_read_u8(p);
```

## Error Handling
```
try { throw "error"; } catch (e) { print(e); } finally { cleanup(); }
panic("unrecoverable");  // not catchable
```

## Async/Concurrency
```
async fn compute(n: i32): i32 { return n * n; }
let task = spawn(compute, 42);
let result = await task;  // or join(task)
detach(spawn(background_work));
let ch = channel(10);
ch.send(value); let val = ch.recv(); ch.close();
```

## Atomics
```
atomic_load_i32(p); atomic_store_i32(p, val);
atomic_add_i32(p, n); atomic_sub_i32(p, n);
atomic_cas_i32(p, expected, desired);  // returns bool
atomic_exchange_i32(p, val);
atomic_fence();
```

## I/O
```
print("hello"); eprint("error");
let line = read_line();  // null on EOF
let f = open("file.txt", "r");  // r, w, a, r+, w+, a+
let content = f.read(); f.write("data"); f.close();
```

## Standard Library (import with @stdlib/ prefix)
```
import { sin, cos, PI } from "@stdlib/math";
import { HashMap, Queue, Set } from "@stdlib/collections";
import { read_file, write_file } from "@stdlib/fs";
import { parse, stringify } from "@stdlib/json";
import { DateTime } from "@stdlib/datetime";
import { base64_encode, hex_encode } from "@stdlib/encoding";
import { sha256 } from "@stdlib/hash";
import { compile, test } from "@stdlib/regex";
```
Key modules: math, collections, fs, json, datetime, encoding, hash, regex, crypto,
csv, net, http, sqlite, strings, path, env, os, time, process, async, random, toml, uuid

## FFI
```
import "libc.so.6";
extern fn strlen(s: string): i32;
```

## Signals
```
signal(SIGINT, fn(sig) { print("caught"); });
```
