# Hemlock Language Reference (Compact)

Systems scripting language. Manual memory, no GC, C-like syntax, semicolons mandatory.

## Types
Signed: i8, i16, i32, i64 | Unsigned: u8, u16, u32, u64 | Floats: f32, f64
Other: bool, string, rune, array, ptr, buffer, null, object, file, task, channel
Aliases: integer=i32, number=f64, byte=u8

Literals: `42` → i32, `5000000000` → i64, `3.14` → f64, `0xFF` hex, `0b1010` bin, `0o777` oct
Numeric separators: `1_000_000`
Type conversion: `i32("42")`, `f64(100)`, `i64(42)`
Introspection: `typeof(x)` returns string, `typeid(x)` returns integer (fast, no allocation)
TYPEID constants: TYPEID_I8(0), TYPEID_I16(1), TYPEID_I32(2), TYPEID_I64(3), TYPEID_U8(4), TYPEID_U16(5), TYPEID_U32(6), TYPEID_U64(7), TYPEID_F32(8), TYPEID_F64(9), TYPEID_BOOL(10), TYPEID_STRING(11), TYPEID_RUNE(12), TYPEID_PTR(13), TYPEID_BUFFER(14), TYPEID_ARRAY(15), TYPEID_OBJECT(16), TYPEID_FILE(17), TYPEID_FUNCTION(18), TYPEID_TASK(19), TYPEID_CHANNEL(20), TYPEID_NULL(21)
String: `"hello".length` (runes), `"hello".byte_length` (bytes)
Array: `[1,2].length`

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
Null coalescing: `x ?? default`, `x ??= fallback`, `obj?.field?.sub`

## Operators
Arithmetic: `+`, `-`, `*`, `/` (ALWAYS returns float! use `divi()` for integer division), `%`
Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
Logical: `&&`, `||`, `!`
Bitwise: `&`, `|`, `^`, `~`, `<<`, `>>`
Increment: `++x`, `x++`, `--x`, `x--`
Compound: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`

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
define Comparable { value: i32, fn compare(other: Self): i32 }  // Self type
let p: Person = { name: "Alice", age: 30 };
let person = { name, age };  // shorthand
let config = { ...defaults, size: "large" };  // spread
enum Color { RED, GREEN, BLUE }
type Callback = fn(i32): void;  // type alias
type Person = HasName & HasAge;  // compound type alias
```

Object bracket notation (keys auto-coerce to string):
```
let map = {};
map[42] = "value";    // integer key → "42"
map.has(42);           // true
map.delete(42);        // removes field
let keys = map.keys(); // array of string keys
```

## Strings (22 methods, mutable via index assignment)
`substr`, `slice`, `find`, `contains`, `split`, `trim`, `trim_start`, `trim_end`,
`to_upper`, `to_lower`, `starts_with`, `ends_with`, `replace`, `replace_all`,
`repeat`, `char_at`, `byte_at`, `chars`, `bytes`, `to_bytes`, `byte_ptr`, `deserialize`
Template strings: `` `Hello ${name}!` ``
Escape sequences: `\n`, `\t`, `\x41` (hex), `\u{1F600}` (unicode)

## Arrays (28 methods)
`push`, `pop`, `shift`, `unshift`, `insert`, `remove`, `find`, `findIndex`, `contains`,
`slice`, `join`, `concat`, `reverse`, `first`, `last`, `clear`, `map`, `filter`, `reduce`,
`every`, `some`, `indexOf`, `lastIndexOf`, `sort`, `fill`, `reserve`, `flat`, `serialize`
Typed: `let nums: array<i32> = [1, 2, 3];`

## Memory
```
let p = alloc(64);       // raw pointer
let b = buffer(64);      // bounds-checked
let view = b.slice(0, 16);  // zero-copy buffer view
memset(p, 0, 64); memcpy(dest, src, 64);
free(p);                 // manual cleanup
ptr_write_i32(p, value); ptr_deref_i32(p);  // pointer read/write (also accept buffers)
ptr_write_u8(p, value); ptr_deref_u8(p);
ptr_offset(p, index, stride);  // returns ptr at p + index * stride bytes
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
let t = spawn_with({ stack_size: 4194304, name: "worker" }, compute, 42);
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
print("hello");          // stdout with newline
write("no newline");     // stdout without newline
eprint("error");         // stderr with newline
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
import { divi } from "@stdlib/math";
```
Key modules: math, collections, fs, json, datetime, encoding, hash, regex, crypto,
csv, net, http, sqlite, strings, path, env, os, time, process, async, random, toml, uuid,
args, assert, atomic, bytes, compression, debug, decimal, ffi, fmt, glob, ipc, iter,
jinja, logging, matrix, mmap, net, retry, semver, shell, signal, terminal, termios,
testing, url, unix_socket, vector, websocket, yaml

## FFI
```
import "libc.so.6";
extern fn strlen(s: string): i32;
```

## Signals
```
signal(SIGINT, fn(sig) { print("caught"); });
```
