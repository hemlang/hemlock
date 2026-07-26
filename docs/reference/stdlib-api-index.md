# Stdlib API Index

<!-- GENERATED FILE - DO NOT EDIT. Regenerate with: make stdlib-api -->

One line per exported symbol across the entire standard library — the
complete importable API surface, generated from `stdlib/*.hml` by
`tests/gen_stdlib_api.py` so it cannot drift from the code. Signatures
are verbatim from source; `[native]` marks re-exported native builtins
whose parameter list comes from the module's documentation.

53 modules, 1050 exported symbols. Import with
`import { name } from "@stdlib/<module>";` — see
[Standard Library Overview](stdlib-overview.md) and `stdlib/docs/` for
full documentation.

## @stdlib/arena

```
fn Arena(capacity: i64)
fn GrowingArena(chunk_size?: 64 * 1024)
fn ScratchScope(arena)
let SIZEOF_I8 = 1  // Size constants for primitive types
let SIZEOF_I16 = 2
let SIZEOF_I32 = 4
let SIZEOF_I64 = 8
let SIZEOF_U8 = 1
let SIZEOF_U16 = 2
let SIZEOF_U32 = 4
let SIZEOF_U64 = 8
let SIZEOF_F32 = 4
let SIZEOF_F64 = 8
let SIZEOF_PTR = 8  // 64-bit pointers
fn arena_alloc_i32_array(arena, count: i64)  // Allocate array of i32 values
fn arena_alloc_i64_array(arena, count: i64)  // Allocate array of i64 values
fn arena_alloc_f64_array(arena, count: i64)  // Allocate array of f64 values
fn arena_alloc_ptr_array(arena, count: i64)  // Allocate array of pointers
fn arena_copy_string(arena, str: string)  // Copy a string into the arena
```

## @stdlib/args

```
fn parse(argv, options?: null): object  // Parse command-line arguments into a structured object
fn has_flag(parsed, name): bool  // Check if a flag is set
fn get_option(parsed, name, default_value?: null)  // Get an option value
fn get_positionals(parsed): array  // Get positional arguments
fn get_script(parsed): string  // Get the script name
fn ArgParser(name, description?: "")  // Create an argument parser with configuration
fn shift(argv, n?: 1): array  // Shift arguments (remove first N elements)
fn join_args(argv, separator?: " "): string  // Join remaining arguments into a string
```

## @stdlib/assert

```
fn assert(condition, message?: "Assertion failed")  // Assert that a condition is true
fn assert_false(condition, message?: "Expected false")  // Assert that a condition is false
fn assert_truthy(value, message?: "Expected truthy value")  // Assert that a value is truthy (not null, false, 0, or "")
fn assert_falsy(value, message?: "Expected falsy value")  // Assert that a value is falsy (null, false, 0, or "")
fn assert_eq(actual, expected, message?: "Values not equal")  // Assert that two values are equal
fn assert_ne(actual, expected, message?: "Values should not be equal")  // Assert that two values are not equal
fn assert_strict_eq(actual, expected, message?: "Values not strictly equal")  // Assert that actual is strictly equal (same type and value)
fn assert_gt(actual, expected, message?: "Expected greater than")  // Assert that actual > expected
fn assert_gte(actual, expected, message?: "Expected greater than or equal")  // Assert that actual >= expected
fn assert_lt(actual, expected, message?: "Expected less than")  // Assert that actual < expected
fn assert_lte(actual, expected, message?: "Expected less than or equal")  // Assert that actual <= expected
fn assert_in_range(value, min_val, max_val, message?: "Value out of range")  // Assert that value is within range [min, max] (inclusive)
fn assert_type(value, expected_type: string, message?: "Type mismatch")  // Assert that value is of a specific type
fn assert_null(value, message?: "Expected null")  // Assert that value is null
fn assert_not_null(value, message?: "Expected non-null")  // Assert that value is not null
fn assert_string(value, message?: "Expected string")  // Assert that value is a string
fn assert_number(value, message?: "Expected number")  // Assert that value is a number (any numeric type)
fn assert_bool(value, message?: "Expected boolean")  // Assert that value is a boolean
fn assert_array(value, message?: "Expected array")  // Assert that value is an array
fn assert_object(value, message?: "Expected object")  // Assert that value is an object
fn assert_function(value, message?: "Expected function")  // Assert that value is a function
fn assert_contains(collection, value, message?: "Value not found")  // Assert that array/string contains a value
fn assert_not_contains(collection, value, message?: "Value should not be present")  // Assert that array/string does not contain a value
fn assert_empty(collection, message?: "Expected empty")  // Assert that array/string is empty
fn assert_not_empty(collection, message?: "Expected non-empty")  // Assert that array/string is not empty
fn assert_length(collection, expected: i32, message?: "Length mismatch")  // Assert array length
fn assert_has_key(obj, key, message?: "Key not found")  // Assert that object has a key
fn assert_starts_with( str: string, prefix: string, message?: "String does not start with prefix", )  // Assert string starts with prefix
fn assert_ends_with(str: string, suffix: string, message?: "String does not end with suffix")  // Assert string ends with suffix
fn assert_matches(str: string, pattern: string, message?: "String does not match pattern")  // Assert string matches regex pattern
fn assert_throws(func, expected_message?: null, message?: "Expected exception")  // Assert that a function throws an error
fn assert_no_throw(func, message?: "Unexpected exception")  // Assert that a function does not throw
fn assert_approx_eq( actual, expected, tolerance?: 0.0001, message?: "Values not approximately equal", )  // Assert that two floats are approximately equal
fn assert_nan(value, message?: "Expected NaN")  // Assert that value is NaN
fn assert_not_nan(value, message?: "Expected non-NaN")  // Assert that value is not NaN
fn assert_finite(value, message?: "Expected finite number")  // Assert that value is finite (not NaN or Infinity)
fn assert_positive(value, message?: "Expected positive number")  // Assert that value is positive
fn assert_negative(value, message?: "Expected negative number")  // Assert that value is negative
fn assert_zero(value, message?: "Expected zero")  // Assert that value is zero
fn assert_array_eq(actual, expected, message?: "Arrays not equal")  // Assert arrays are equal (deep comparison)
fn assert_includes_all(arr, expected, message?: "Missing values")  // Assert that array includes all expected values
```

## @stdlib/async

```
fn get_default_stack_size(): i64  // Get the current default stack size for spawned threads (in bytes)
fn set_default_stack_size(size: i64)  // Set the default stack size for all subsequent spawn() calls (in bytes)
fn ThreadPool(num_workers: i32)  // A fixed-size thread pool for executing async tasks
fn parallel_map(arr, map_fn, num_workers?: 4)
```

## @stdlib/async_fs

```
fn async_read_file(path: string)  // Read entire file contents asynchronously
fn async_write_file(path: string, content: string)  // Write content to file asynchronously
fn async_append_file(path: string, content: string)  // Append content to file asynchronously
fn async_copy_file(src: string, dst: string)  // Copy file asynchronously
fn async_remove_file(path: string)  // Remove file asynchronously
fn async_rename(old_path: string, new_path: string)  // Rename/move file asynchronously
fn async_exists(path: string)  // Check if file/directory exists asynchronously
fn async_file_stat(path: string)  // Get file stat asynchronously
fn async_list_dir(path: string)  // List directory contents asynchronously
fn async_make_dir(path: string)  // Create directory asynchronously
fn async_remove_dir(path: string)  // Remove directory asynchronously
fn read_files_parallel(paths)  // Read multiple files in parallel
fn write_files_parallel(files)  // Write multiple files in parallel
fn copy_files_parallel(copies)  // Copy multiple files in parallel
fn shutdown_async_fs()  // Shutdown the internal thread pool (call when done with async file I/O)
```

## @stdlib/atomic

```
fn atomic_load_i32(ptr)  [native]  // Atomic read
fn atomic_store_i32(ptr, value)  [native]
fn atomic_add_i32(ptr, value)  [native]
fn atomic_sub_i32(ptr, value)  [native]
fn atomic_and_i32(ptr, value)  [native]
fn atomic_or_i32(ptr, value)  [native]
fn atomic_xor_i32(ptr, value)  [native]
fn atomic_cas_i32(ptr, expected, desired)  [native]
fn atomic_exchange_i32(ptr, value)  [native]
fn atomic_load_i64(ptr)  [native]
fn atomic_store_i64(ptr, value)  [native]
fn atomic_add_i64(ptr, value)  [native]
fn atomic_sub_i64(ptr, value)  [native]
fn atomic_and_i64(ptr, value)  [native]
fn atomic_or_i64(ptr, value)  [native]
fn atomic_xor_i64(ptr, value)  [native]
fn atomic_cas_i64(ptr, expected, desired)  [native]
fn atomic_exchange_i64(ptr, value)  [native]
fn atomic_fence()  [native]  // Full memory barrier (sequentially consistent)
fn AtomicI32(initial?: 0)  // AtomicI32 - Atomic 32-bit integer counter
fn AtomicI64(initial?: 0)  // AtomicI64 - Atomic 64-bit integer counter
fn SpinLock()  // SpinLock - Simple spin lock using atomic CAS
```

## @stdlib/bytes

```
fn bswap16(val: u16): u16  [native]  // Reverse bytes of 16-bit value
fn bswap32(val: u32): u32  [native]
fn bswap64(val: u64): u64  [native]
fn htons(val: u16): u16  [native]  // Host to network byte order (16-bit)
fn htonl(val: u32): u32  [native]
fn htonll(val: u64): u64  [native]
fn ntohs(val: u16): u16  [native]  // Network to host byte order (16-bit)
fn ntohl(val: u32): u32  [native]
fn ntohll(val: u64): u64  [native]
fn is_little_endian(): bool  [native]  // True if host is little-endian
fn read_u16_be(p: ptr, offset: i32): u16  [native]  // Read big-endian u16 from buffer
fn read_u16_le(p: ptr, offset: i32): u16  [native]
fn read_u32_be(p: ptr, offset: i32): u32  [native]
fn read_u32_le(p: ptr, offset: i32): u32  [native]
fn read_u64_be(p: ptr, offset: i32): u64  [native]
fn read_u64_le(p: ptr, offset: i32): u64  [native]
fn write_u16_be(p: ptr, offset: i32, val: u16): void  [native]  // Write big-endian u16 to buffer
fn write_u16_le(p: ptr, offset: i32, val: u16): void  [native]
fn write_u32_be(p: ptr, offset: i32, val: u32): void  [native]
fn write_u32_le(p: ptr, offset: i32, val: u32): void  [native]
fn write_u64_be(p: ptr, offset: i32, val: u64): void  [native]
fn write_u64_le(p: ptr, offset: i32, val: u64): void  [native]
fn f32_to_bits(val: f32): u32  // Get the IEEE 754 bit pattern of an f32
fn f32_from_bits(bits: u32): f32  // Build an f32 from an IEEE 754 bit pattern
fn f64_to_bits(val: f64): u64  // Get the IEEE 754 bit pattern of an f64
fn f64_from_bits(bits: u64): f64  // Build an f64 from an IEEE 754 bit pattern
fn to_hex(buf, len: i32): string  // Convert a buffer to a hex string
fn from_hex(hex: string): buffer  // Convert a hex string to a buffer
fn compare(a, b, len: i32): i32  // Compare two buffers lexicographically
fn fill(buf, val: u8, len: i32)  // Fill a buffer with a byte value
fn copy(dest, src, len: i32)  // Copy bytes between buffers
```

## @stdlib/collections

```
fn HashMap()  // Hash table implementation with separate chaining
fn Queue()
fn Stack()
fn Set()
fn LinkedList()
fn LRUCache(capacity: i32)
```

## @stdlib/compression

```
let Z_NO_COMPRESSION = 0  // Compression levels
let Z_BEST_SPEED = 1
let Z_BEST_COMPRESSION = 9
let Z_DEFAULT_COMPRESSION = -1
let LEVEL_NONE = 0  // Compression levels (aliases for convenience)
let LEVEL_FASTEST = 1
let LEVEL_FAST = 3
let LEVEL_DEFAULT = 6
let LEVEL_BEST = 9
fn deflate_compress(data: string, level?: 6)  // Compress data using raw deflate format
fn inflate_decompress(data, max_size?: 10485760)  // Decompress raw deflate data
fn gzip(data: string, level?: 6)  // Compress data to gzip format
fn gunzip(data, max_size?: 10485760)  // Decompress gzip data
fn compress(data: string, level?: 6)  // Alias for deflate_compress
fn decompress(data, max_size?: 10485760)  // Alias for inflate_decompress
fn compress_bound(source_len: i64)  // Calculate maximum compressed size for given input length
fn crc32(data)  // Calculate CRC32 checksum of buffer data
fn adler32(data)  // Calculate Adler-32 checksum of buffer data
let TAR_TYPE_FILE  // File types
let TAR_TYPE_HARDLINK
let TAR_TYPE_SYMLINK
let TAR_TYPE_CHARDEV
let TAR_TYPE_BLOCKDEV
let TAR_TYPE_DIRECTORY
let TAR_TYPE_FIFO
fn TarWriter()  // Create a TarWriter for building tar archives
fn TarReader(data)  // Create a TarReader for reading tar archives
fn gzip_file(input_path: string, output_path: string, level?: 6)  // Compress a file to gzip format
fn gunzip_file(input_path: string, output_path: string, max_size?: 10485760)  // Decompress a gzip file
```

## @stdlib/crypto

```
fn random_bytes(size)  // Generate cryptographically secure random bytes
fn generate_aes_key()  // Generate a 256-bit (32 byte) AES key
fn generate_iv()  // Generate a 128-bit (16 byte) initialization vector (IV)
fn aes_encrypt(plaintext, key, iv)  // AES-256-CBC Encryption
fn aes_decrypt(ciphertext, key, iv)  // AES-256-CBC Decryption
fn rsa_generate_key()  // Generate RSA-2048 key pair
fn rsa_free_keys(keypair)  // Free RSA key pair
fn rsa_sign(data, keypair)  // Sign data with RSA private key using SHA-256
fn rsa_verify(data, signature, keypair)  // Verify RSA signature with public key using SHA-256
fn ecdsa_generate_key(curve?: null)  // Generate ECDSA P-256 key pair (default) or other curve
fn ecdsa_free_keys(keypair)  // Free ECDSA key pair
fn ecdsa_sign(data, keypair)  // Sign data with ECDSA private key using SHA-256
fn ecdsa_verify(data, signature, keypair)  // Verify ECDSA signature with public key using SHA-256
fn buffer_to_hex(buf)  // Convert buffer to hexadecimal string (useful for displaying keys/signatures)
fn hex_to_buffer(hex)  // Convert hexadecimal string to buffer
```

## @stdlib/csv

```
fn parse(text, options?: null): array  // Parse a CSV string into an array of rows (each row is an array of strings)
fn parse_objects(text, options?: null): array  // Parse CSV with headers - returns array of objects
fn parse_row(line, options?: null): array  // Parse a single CSV row
fn stringify(rows, options?: null): string  // Convert an array of rows to CSV string
fn stringify_objects(objects, headers, options?: null): string  // Convert an array of objects to CSV string
fn stringify_row(row, options?: null): string  // Convert a single row to CSV line
fn get_column(rows, index): array  // Get column from CSV data
fn row_count(rows): i32  // Get row count
fn column_count(rows): i32  // Get column count (from first row)
```

## @stdlib/datetime

```
fn localtime(...)  [native]  // Convert Unix timestamp to local time components
fn gmtime(...)  [native]  // Convert Unix timestamp to UTC components
fn mktime(...)  [native]  // Convert time components to Unix timestamp
fn strftime(...)  [native]  // Format date/time with strftime format string
fn DateTime(timestamp?: null)  // DateTime constructor - creates from Unix timestamp
fn now()  // Create DateTime from current time
fn from_date( year: i32, month: i32, day: i32, hour?: 0, minute?: 0, second?: 0, )  // Create DateTime from date components (local time)
fn from_utc(year: i32, month: i32, day: i32, hour?: 0, minute?: 0, second?: 0)  // Create DateTime from UTC date components
fn parse_iso(date_str: string)  // Parse ISO 8601 date string (YYYY-MM-DD or YYYY-MM-DDTHH:MM:SS)
fn is_leap_year(year: i32): bool  // Check if a year is a leap year
fn days_in_month(year: i32, month: i32): i32  // Get the number of days in a given month
fn days_in_year(year: i32): i32  // Get the number of days in a given year
fn is_valid_date(year: i32, month: i32, day: i32): bool  // Validate date components
fn is_valid_time(hour: i32, minute: i32, second: i32): bool  // Validate time components
```

## @stdlib/debug

```
fn task_debug_info(task)  [native]  // Get debug info for a task (state, stack usage)
fn set_stack_limit(bytes)  [native]
fn get_stack_limit()  [native]
fn inspect(val): string  // Inspect a value, returning a detailed string with type information
fn hexdump(buf, len: i32): string  // Hexdump a buffer for debugging
```

## @stdlib/decimal

```
fn to_fixed(num, places?: 0): string  // Format number to fixed decimal places with rounding
fn to_precision(num, digits): string  // Format number to N significant figures
fn to_hex(num): string  // Convert integer to hexadecimal string
fn to_oct(num): string  // Convert integer to octal string
fn to_bin(num): string  // Convert integer to binary string
fn number_to_string(num, radix?: 10): string  // Convert integer to string in arbitrary base (2-36)
fn parse_int(str, radix?: 10): i64  // Parse string to integer with optional radix
fn parse_float(str): f64  // Parse string to floating-point number
fn sb_new(): object  // Create a new StringBuilder
fn sb_append(sb, val): object  // Append a value to the StringBuilder
fn sb_prepend(sb, val): object  // Prepend a value to the StringBuilder
fn sb_to_string(sb): string  // Convert StringBuilder to string
fn sb_join(sb, sep): string  // Convert StringBuilder to string with separator
fn sb_count(sb): i32  // Get number of parts in the StringBuilder
fn sb_clear(sb): object  // Clear the StringBuilder
```

## @stdlib/encoding

```
fn base32_encode(input)  // Encode string to Base32
fn base32_decode(input)  // Decode Base32 string to original string
fn base58_encode(input)  // Encode string to Base58
fn base58_decode(input)  // Decode Base58 string to original string
```

## @stdlib/env

```
fn getenv(name)  [native]  // Get environment variable (returns null if not found)
fn setenv(name, value)  [native]  // Set environment variable
fn unsetenv(name)  [native]  // Unset environment variable
fn exit(code?)  [native]
fn get_pid()  [native]
```

## @stdlib/ffi

```
fn callback(fn, param_types, return_type)  [native]  // Create a C-callable function pointer from a Hemlock function
fn callback_free(cb)  [native]
fn ffi_sizeof(...)  [native]
let FFI_VOID = 0  // Common libffi type constants for use with ffi_bind()
let FFI_INT = 1
let FFI_FLOAT = 2
let FFI_DOUBLE = 3
let FFI_POINTER = 4
let FFI_STRING = 5
let FFI_INT8 = 6
let FFI_INT16 = 7
let FFI_INT32 = 8
let FFI_INT64 = 9
let FFI_UINT8 = 10
let FFI_UINT16 = 11
let FFI_UINT32 = 12
let FFI_UINT64 = 13
```

## @stdlib/fmt

```
fn format(template, args): string  // Format a string with placeholders
fn sprintf(template, args): string  // Alias for format
let pad_left
let pad_right
let center
let truncate
fn wrap(text, width): string  // Wrap text to specified width
fn thousands(num, sep?: ","): string  // Format number with thousands separator
fn bytes_size(bytes, precision?: 1): string  // Format bytes as human-readable size
fn duration(seconds): string  // Format duration in seconds as human-readable
fn ordinal(n): string  // Format number as ordinal (1st, 2nd, 3rd, etc.)
fn percent(value, precision?: 1): string  // Format percentage
```

## @stdlib/fs

```
fn open(path, mode?)  [native]
fn open_fd(path, mode?)  [native]
fn fileno(file)  [native]
fn exists(path)  [native]
fn read_file(path)  [native]
fn write_file(path, content)  [native]
fn append_file(path, content)  [native]
fn remove_file(path)  [native]
fn rename(old_path, new_path)  [native]
fn copy_file(src, dest)  [native]
fn make_dir(path, mode?)  [native]
fn remove_dir(path)  [native]
fn list_dir(path)  [native]
fn make_dirs(path: string, mode?: null)  // Create a directory and any missing parent directories. Existing directories
fn is_file(path)  [native]
fn is_dir(path)  [native]
fn file_stat(path)  [native]
fn cwd()  [native]
fn chdir(path)  [native]
fn absolute_path(path)  [native]
```

## @stdlib/glob

```
fn glob_match(pattern, text): bool  // Match a string against a glob pattern
fn match_path(pattern, path): bool  // Match a path against a glob pattern with ** support
fn glob(pattern, base_dir?: "."): array  // Find files matching a glob pattern
fn escape(text): string  // Escape special glob characters in a string
fn has_magic(pattern): bool  // Check if a pattern contains any glob special characters
fn filter(paths, pattern): array  // Filter a list of paths by a glob pattern
fn translate(pattern): string  // Translate a glob pattern to a regular expression string
```

## @stdlib/hash

```
fn djb2(input: string): u32  // DJB2 Hash Algorithm
fn fnv1a(input: string): u32  // FNV-1a Hash Algorithm (32-bit version)
fn murmur3(input: string, seed?: 0): u32  // MurmurHash3 (32-bit version, simplified)
fn crc32(input): u32  // CRC32 checksum (32-bit)
fn adler32(input): u32  // Adler-32 checksum (32-bit)
fn sha1(input: string): string  // SHA-1 hash (160-bit / 20-byte output)
fn sha256(input: string): string  // SHA-256 hash (256-bit / 32-byte output)
fn sha512(input: string): string  // SHA-512 hash (512-bit / 64-byte output)
fn md5(input: string): string  // MD5 hash (128-bit / 16-byte output)
fn file_checksum(path: string, hash_fn): string  // Compute hash of file contents
fn file_crc32(path: string): string
fn file_adler32(path: string): string
fn file_sha1(path: string): string
fn file_sha256(path: string): string
fn file_sha512(path: string): string
fn file_md5(path: string): string
fn file_djb2(path: string): string
fn file_fnv1a(path: string): string
fn file_murmur3(path: string): string
fn hmac_sha1(key: string, message: string): string  // HMAC-SHA1: Keyed-hash message authentication code using SHA-1
fn hmac_sha256(key: string, message: string): string  // HMAC-SHA256: Keyed-hash message authentication code using SHA-256
fn hmac_sha512(key: string, message: string): string  // HMAC-SHA512: Keyed-hash message authentication code using SHA-512
fn hmac_md5(key: string, message: string): string  // HMAC-MD5: Keyed-hash message authentication code using MD5
```

## @stdlib/http

```
fn get(url, headers)
fn get_binary(url, headers)  // Get binary data (for file downloads - returns buffer instead of string)
fn post(url, body, headers)
fn put(url, body, headers)
fn delete(url, headers)
fn request(method, url, body, headers)
fn fetch(url)
fn post_json(url, data)
fn get_json(url)
fn download(url, output_path)  // Buffered download — fetches the whole body into memory then writes
fn download_streaming(url, output_path)  // Streaming download — opens an HTTP stream, writes binary chunks to
fn is_success(status_code)
fn is_redirect(status_code)
fn is_client_error(status_code)
fn is_server_error(status_code)
fn url_encode(str)
fn get_timeout(url, headers, timeout_ms)  // GET request with custom timeout
fn post_timeout(url, body, headers, timeout_ms)  // POST request with custom timeout
fn put_timeout(url, body, headers, timeout_ms)  // PUT request with custom timeout
fn delete_timeout(url, headers, timeout_ms)  // DELETE request with custom timeout
fn request_timeout(method, url, body, headers, timeout_ms)  // Generic request with custom timeout
fn post_json_timeout(url, data, timeout_ms)  // POST JSON with custom timeout (useful for LLM APIs)
fn stream(method, url, body, headers, timeout_ms)  // Open a streaming HTTP connection that returns data incrementally.
fn stream_get(url, headers, timeout_ms)  // Stream a GET request
fn stream_post(url, body, headers, timeout_ms)  // Stream a POST request (useful for LLM APIs)
fn post_json_stream(url, data, timeout_ms)  // POST JSON and stream the response (convenience for LLM APIs)
fn stream_sse(url, headers, timeout_ms)  // Stream Server-Sent Events (SSE)
fn HttpServer(host: string, port: i32)  // HttpServer - Simple HTTP server for handling requests
```

## @stdlib/ipc

```
fn MessageQueue(path): object  // Create a file-based message queue
fn Semaphore(path, initial?: 1): object  // Create a file-based semaphore
fn Mutex(path): object  // Create a file-based mutex
fn SharedData(path): object  // Create a shared data store (file-based key-value)
fn PidFile(path): object  // Create a PID file for process identification
fn Event(path): object  // Create an event file for simple signaling between processes
fn Pipe(): object  // Create an OS-level pipe for inter-process communication
fn pipe_create(): object  // Create a raw pipe, returns { read_fd: i32, write_fd: i32 }
fn fd_read(fd: i32, size?: 4096): string  // Read from a file descriptor
fn fd_write(fd: i32, data: string): i32  // Write to a file descriptor
fn fd_close(fd: i32)  // Close a file descriptor
fn dup2(oldfd: i32, newfd: i32): i32  // Duplicate a file descriptor
let STDIN_FD  [native]  // Standard file descriptor constants
let STDOUT_FD  [native]
let STDERR_FD  [native]
```

## @stdlib/iter

```
fn range(start_or_end, end_val?: null, step?: 1): array  // Generate a range of integers
fn frange(start, end, step): array  // Generate a range of floats
fn repeat(value, n): array  // Repeat a value n times
fn enumerate(arr, start?: 0): array  // Enumerate an array, returning [index, value] pairs
fn zip(a, b): array  // Zip two arrays together into pairs
fn zip3(a, b, c): array  // Zip three arrays together into triples
fn unzip(pairs): array  // Unzip an array of pairs into two arrays
fn chunk(arr, size): array  // Split array into chunks of given size
fn take(arr, n): array  // Take first n elements from array
fn drop(arr, n): array  // Drop first n elements from array
fn take_last(arr, n): array  // Take last n elements from array
fn drop_last(arr, n): array  // Drop last n elements from array
fn map(arr, f): array  // Apply a function to each element and return new array
fn filter(arr, pred): array  // Return elements that satisfy a predicate
fn reduce(arr, f, initial)  // Reduce array to a single value by applying a function
fn scan(arr, f, initial): array  // Return cumulative reduction results (prefix sums, etc.)
fn each(arr, f)  // Apply function to each element (side effects only, no return)
fn flat_map(arr, f): array  // Map and flatten one level (flatMap)
fn compact(arr): array  // Remove null values from array
fn sort_by(arr, cmp): array  // Sort array using a comparison function
fn zip_with(a, b, f): array  // Zip two arrays using a combining function
fn window(arr, size): array  // Sliding window over array
fn flatten(arr): array  // Flatten a nested array one level deep
fn flatten_deep(arr): array  // Deep flatten a nested array
fn unique(arr): array  // Return unique elements from array (preserves order)
fn group_by(arr, key_fn): object  // Group elements by a key function
fn partition(arr, pred): array  // Partition array by a predicate function
fn sum(arr)  // Sum all elements in array
fn product(arr)  // Product of all elements in array
fn min_val(arr)  // Find minimum value in array
fn max_val(arr)  // Find maximum value in array
fn average(arr): f64  // Calculate average of array elements
fn all(arr, pred): bool  // Check if all elements satisfy predicate
fn any(arr, pred): bool  // Check if any element satisfies predicate
fn none(arr, pred): bool  // Check if no elements satisfy predicate
fn count(arr, pred): i32  // Count elements that satisfy predicate
fn find_first(arr, pred)  // Find first element that satisfies predicate
fn find_index(arr, pred): i32  // Find index of first element that satisfies predicate
```

## @stdlib/jinja

```
fn render(template, context): string  // Render a Jinja-style template with the given context
fn escape(str): string  // Escape HTML special characters
fn namespace(initial): object  // Create a namespace object (mutable object for use in loops)
```

## @stdlib/json

```
fn parse(json_str: string)  // Parse JSON string to value (wrapper around built-in .deserialize())
fn stringify(value)  // Serialize value to JSON string
fn parse_file(path: string)  // Parse JSON from file
fn stringify_file(path: string, value)  // Write JSON to file (compact format)
fn pretty(value, indent?: null)  // Pretty print JSON with indentation
fn pretty_file(path: string, value, indent?: null)  // Pretty print JSON to file
fn get(obj, path: string, default_val?: null)  // Get value by path (dot notation)
fn set(obj, path: string, value)  // Set value by path (dot notation)
fn has(obj, path: string): bool  // Check if path exists
fn delete(obj, path: string): bool  // Delete value by path (dot notation)
fn is_valid(json_str: string): bool  // Check if string is valid JSON (lightweight check)
fn validate(json_str: string)  // Validate JSON and return detailed result
fn is_object(value): bool
fn is_array(value): bool
fn is_string(value): bool
fn is_number(value): bool
fn is_bool(value): bool
fn is_null(value): bool
fn type_of(value): string  // Get JSON type as string
fn clone(value)  // Deep clone value (creates independent copy)
fn merge(base, update)  // Deep merge objects (combines nested objects)
```

## @stdlib/logging

```
let DEBUG = 0  // Log level constants (lower number = more verbose)
let INFO = 1
let WARN = 2
let ERROR = 3
fn Logger(config?: null)  // Logger(config?: object) -> Logger object
let default_logger  // Create a default logger instance for convenience
fn debug(message, data?: null)  // Export convenience functions that use the default logger
fn info(message, data?: null)
fn warn(message, data?: null)
fn error(message, data?: null)
fn log(level: i32, message, data?: null)
```

## @stdlib/math

```
let PI  [native]
let E  [native]
let TAU  [native]
let INF  [native]
let NAN  [native]
fn sin(x)  [native]
fn cos(x)  [native]
fn tan(x)  [native]
fn asin(x)  [native]
fn acos(x)  [native]
fn atan(x)  [native]
fn atan2(y, x)  [native]
fn sqrt(x)  [native]
fn pow(base, exponent)  [native]
fn exp(x)  [native]
fn log(x)  [native]  // Natural logarithm (ln)
fn log10(x)  [native]  // Base-10 logarithm
fn log2(x)  [native]  // Base-2 logarithm
fn div(a, b)  [native]  // Float division (same as / operator)
fn divi(a, b)  [native]  // Integer division (truncates toward zero)
fn floor(x)  [native]
fn ceil(x)  [native]
fn round(x)  [native]
fn trunc(x)  [native]
fn floori(x)  [native]  // Integer-returning rounding variants
fn ceili(x)  [native]
fn roundi(x)  [native]
fn trunci(x)  [native]
fn abs(x)  [native]
fn min(a, b)  [native]
fn max(a, b)  [native]
fn clamp(value, min_val, max_val)  [native]
fn sign(x): i32  // Return -1, 0, or 1 based on sign of value
fn cbrt(x): f64  // Cube root
fn hypot(x, y): f64  // Hypotenuse: sqrt(x² + y²) without overflow
fn gcd(a, b): i64  // Greatest common divisor
fn lcm(a, b): i64  // Least common multiple
fn sinh(x): f64
fn cosh(x): f64
fn tanh(x): f64
fn rand()  [native]  // Random float [0.0, 1.0)
fn rand_range(min_val, max_val)  [native]  // Random float [min, max)
fn seed(value)  [native]  // Seed random number generator
```

## @stdlib/matrix

```
fn Matrix(rows: i32, cols: i32)  // Create a matrix with given dimensions, initialized to zero
fn from_array(rows: i32, cols: i32, values: array)  // Create a matrix from a flat array of values (row-major order)
fn from_rows(row_arrays: array)  // Create a matrix from an array of row arrays
fn identity(n: i32)  // Create an NxN identity matrix
fn zeros(rows: i32, cols: i32)  // Create a matrix of zeros
fn ones(rows: i32, cols: i32)  // Create a matrix of ones
fn diagonal(values: array)  // Create a diagonal matrix from an array of values
fn col_vector(values: array)  // Create a column vector (Nx1 matrix) from an array
fn row_vector(values: array)  // Create a row vector (1xN matrix) from an array
fn solve(A, b)  // Solve linear system Ax = b using Gaussian elimination with partial pivoting
```

## @stdlib/mmap

```
fn mmap_open(path, mode?)  [native]  // mmap_open(path: string, mode?: "r"): ptr
fn mmap_open_anon(size)  [native]  // mmap_open_anon(size: i32|i64): ptr
fn mmap_sync(ptr)  [native]  // mmap_sync(ptr: ptr): bool
fn mmap_close(ptr)  [native]  // mmap_close(ptr: ptr): bool
fn mmap_size(ptr)  [native]  // mmap_size(ptr: ptr): i64
fn mmap_advise(ptr, advice)  [native]  // mmap_advise(ptr: ptr, advice: i32): bool
fn mmap_protect(ptr, prot)  [native]  // mmap_protect(ptr: ptr, prot: i32): bool
let PROT_NONE  [native]
let PROT_READ  [native]
let PROT_WRITE  [native]
let PROT_EXEC  [native]
let MADV_NORMAL  [native]
let MADV_SEQUENTIAL  [native]
let MADV_RANDOM  [native]
let MADV_WILLNEED  [native]
let MADV_DONTNEED  [native]
fn MappedFile(path: string, mode?: "r")  // MappedFile(path: string, mode?: "r") - High-level memory-mapped file object
```

## @stdlib/net

```
let AF_INET  [native]  // Address families
let AF_INET6  [native]
let SOCK_STREAM  [native]  // Socket types
let SOCK_DGRAM  [native]
let IPPROTO_TCP  [native]  // Protocols
let IPPROTO_UDP  [native]
let SOL_SOCKET  [native]  // Socket options
let SO_REUSEADDR  [native]
let SO_KEEPALIVE  [native]
let SO_RCVTIMEO  [native]
let SO_SNDTIMEO  [native]
let POLLIN  [native]  // Poll constants
let POLLOUT  [native]
let POLLERR  [native]
let POLLHUP  [native]
let POLLNVAL  [native]
let POLLPRI  [native]
fn socket_create(domain, type, protocol)  [native]  // Networking functions
fn dns_resolve(hostname)  [native]
fn poll(fds, nfds, timeout)  [native]
```

## @stdlib/os

```
fn platform()  [native]  // Get platform name: "linux", "macos", "windows", or "unknown"
fn arch()  [native]  // Get CPU architecture: "x86_64", "aarch64", "arm", etc.
fn os_name()  [native]  // Get OS kernel name: "Linux", "Darwin", "Windows_NT", etc.
fn os_version()  [native]  // Get OS kernel version string
fn hostname()  [native]  // Get system hostname
fn username()  [native]  // Get current username
fn homedir()  [native]  // Get home directory path
fn tmpdir()  [native]  // Get temporary directory path
fn uptime()  [native]  // Get system uptime in seconds
fn cpu_count()  [native]  // Get number of CPU cores (logical processors)
fn total_memory()  [native]  // Get total system memory in bytes
fn free_memory()  [native]  // Get available/free system memory in bytes
fn is_linux(): bool  // Check if running on Linux
fn is_macos(): bool  // Check if running on macOS
fn is_windows(): bool  // Check if running on Windows
fn is_unix(): bool  // Check if running on a Unix-like system (Linux or macOS)
fn info(): object  // Get system info as an object
fn memory_info(): object  // Get memory info as an object (in bytes)
fn format_bytes(bytes: i64): string  // Format bytes as human-readable string (KB, MB, GB, etc.)
```

## @stdlib/path

```
let SEP = "/"  // Path separator for current platform
let DELIMITER = ":"  // Path delimiter for current platform (PATH environment variable separator)
fn join(a, b): string  // Join two path segments with the platform separator
fn join_all(parts): string  // Join multiple path segments with the platform separator
fn dirname(path): string  // Get the directory name of a path
fn basename(path, suffix?: ""): string  // Get the base name of a path (final component)
fn extname(path): string  // Get the extension of a path (including the dot)
fn normalize(path): string  // Normalize a path, resolving . and .. segments
fn is_absolute(path): bool  // Check if a path is absolute
fn resolve(path): string  // Resolve a path to an absolute path (relative to cwd)
fn resolve_all(paths): string  // Resolve a sequence of paths to an absolute path
fn relative(source, target): string  // Get relative path from one path to another
fn parse(path): object  // Parse a path into its components
fn format(pathObject): string  // Format a path object into a path string
fn expand_user(path): string  // Expand ~ and ~user to home directory
fn has_trailing_sep(path): bool  // Check if path has a trailing separator
fn ensure_trailing_sep(path): string  // Ensure path has a trailing separator
fn remove_trailing_sep(path): string  // Remove trailing separator from path
fn matches(path, pattern): bool  // Check if a path matches a simple pattern (glob-like)
```

## @stdlib/process

```
fn get_pid(): i32  [native]  // Process identification
fn get_ppid(...)  [native]
fn get_uid(...)  [native]
fn get_euid(...)  [native]
fn get_gid(...)  [native]
fn get_egid(...)  [native]
fn getppid(): i32  [native]  // Legacy C-style aliases (deprecated, use snake_case versions above)
fn getuid(): i32  [native]
fn geteuid(): i32  [native]
fn getgid(): i32  [native]
fn getegid(): i32  [native]
fn exit(code?: i32)  [native]  // Process control
fn kill(pid: i32, signal: i32)  [native]
fn abort()  [native]
fn fork(): i32  [native]  // Process creation
fn wait(): object  [native]
fn waitpid(pid, options?: 0)  // Wrapper so the options argument really is optional in both backends
fn exec(cmd, args?: null)  // Command execution
fn exec_argv(argv: array<string>): object  [native]  // Safe command execution (no shell, captures stderr)
fn posix_spawn(argv, opts?: null)  // Detached spawn primitive backed by posix_spawn(3).
```

## @stdlib/random

```
fn set_seed(s)  // Seed the random number generator
fn randint(min_val, max_val): i32  // Random integer in range [min, max] (inclusive)
fn randf(min_val, max_val): f64  // Random float in range [min, max)
fn shuffle(arr): array  // Shuffle an array in-place using Fisher-Yates algorithm
fn choice(arr)  // Return a random element from an array
fn sample(arr, n): array  // Return n random elements from an array without replacement
fn choices(arr, n): array  // Return n random elements from an array with replacement
fn weighted_choice(items, weights)  // Choose a random element with weighted probability
fn coin_flip(): bool  // Flip a coin (50/50 chance)
fn dice(sides?: 6): i32  // Roll a die
fn roll(count, sides?: 6): i32  // Roll multiple dice and sum the results
fn random_bool(probability?: 0.5): bool  // Generate a random boolean with given probability of true
fn random_string( length, charset?: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", ): string  // Generate a random string of given length
fn random_hex(length): string  // Generate a random hex string
fn uuid4(): string  // Generate a UUID v4 (random)
```

## @stdlib/regex

```
let REG_EXTENDED = 1  // Use extended regex syntax
let REG_ICASE = 2  // Case-insensitive matching
let REG_NOSUB = 4  // Don't report match positions
let REG_NEWLINE = 8  // Treat newline as special
let REG_NOTBOL = 1  // String is not beginning of line
let REG_NOTEOL = 2  // String is not end of line
let REG_NOMATCH = 1  // No match found
let REG_BADPAT = 2  // Invalid regex pattern
let REG_ECOLLATE = 3  // Invalid collation element
let REG_ECTYPE = 4  // Invalid character class
let REG_EESCAPE = 5  // Trailing backslash
let REG_ESUBREG = 6  // Invalid back reference
let REG_EBRACK = 7  // Brackets [] not balanced
let REG_EPAREN = 8  // Parentheses () not balanced
let REG_EBRACE = 9  // Braces {} not balanced
let REG_BADBR = 10  // Invalid repetition count
let REG_ERANGE = 11  // Invalid range in []
let REG_ESPACE = 12  // Out of memory
let REG_BADRPT = 13  // Invalid use of repetition operator
fn compile(pattern: string, flags?: null): object  // Create a new compiled regex object
fn test(pattern: string, text: string, flags?: null): bool  // Test if string matches pattern (one-shot, no compilation needed for reuse)
fn matches(pattern: string, text: string, flags?: null): bool  // Match string against pattern (one-shot)
fn find(pattern: string, text: string, flags?: null): bool  // Find if pattern exists in string (one-shot)
fn replace(pattern: string, text: string, replacement: string, flags?: null): string  // Replace first match in text (one-shot)
fn replace_all(pattern: string, text: string, replacement: string, flags?: null): string  // Replace all matches in text (one-shot)
fn error_message(errcode: i32): string  // Get error message for a regex error code
```

## @stdlib/retry

```
fn exponential_backoff(attempt, base_delay?: 100, max_delay?: 30000, multiplier?: 2): i32  // Calculate exponential backoff delay
fn linear_backoff(attempt, base_delay?: 100, increment?: 1000, max_delay?: 30000): i32  // Calculate linear backoff delay
fn constant_backoff(delay): i32  // Return constant delay (no backoff)
fn add_jitter(delay, jitter_factor?: 0.1): i32  // Add jitter to a delay to prevent thundering herd
fn retry(func, max_attempts?: 3)  // Retry a function until it succeeds or max attempts reached
fn retry_with_backoff(func, options?: null)  // Retry with configurable backoff between attempts
fn retry_linear(func, max_attempts?: 3, delay?: 1000)  // Retry with linear backoff
fn retry_if(func, should_retry, max_attempts?: 3, delay?: 1000)  // Retry only if error matches condition
fn retry_until(func, condition, options?: null)  // Retry until a condition is met (for polling)
fn with_retry(func, options?: null)  // Create a retry-wrapped version of a function
```

## @stdlib/semver

```
fn parse(version: string): object  // Parse a semantic version string into an object
fn format(ver): string  // Format a version object back to string
fn compare(a, b): i32  // Compare two versions
fn lt(a, b): bool  // Check if a < b
fn lte(a, b): bool  // Check if a <= b
fn gt(a, b): bool  // Check if a > b
fn gte(a, b): bool  // Check if a >= b
fn eq(a, b): bool  // Check if a == b
fn neq(a, b): bool  // Check if a != b
fn increment(version, release: string, identifier?: "0"): string  // Increment a version
fn satisfies(version: string, range: string): bool  // Check if a version satisfies a range
fn major(version: string): i32  // Get the major version number
fn minor(version: string): i32  // Get the minor version number
fn patch(version: string): i32  // Get the patch version number
fn prerelease(version: string): string  // Get the prerelease string
fn valid(version: string): bool  // Check if version is valid
fn clean(version: string): string  // Clean a version string (normalize it)
fn max(a: string, b: string): string  // Compare and return the maximum version
fn min(a: string, b: string): string  // Compare and return the minimum version
fn sort(versions): array  // Sort an array of versions
fn rsort(versions): array  // Reverse sort (highest first)
fn max_satisfying(versions, range: string)  // Get the highest version that satisfies a range
fn min_satisfying(versions, range: string)  // Get the lowest version that satisfies a range
fn diff(a: string, b: string)  // Calculate the difference between two versions
```

## @stdlib/shell

```
fn escape(s: string): string  // Escape special shell characters in a string
fn quote(s: string): string  // Quote a string for safe shell use (single quotes)
fn double_quote(s: string): string  // Quote a string using double quotes (allows variable expansion)
fn build_command(parts): string  // Build a command string from parts
fn and_then(commands): string  // Join commands with && (run second only if first succeeds)
fn or_else(commands): string  // Join commands with || (run second only if first fails)
fn sequential(commands): string  // Join commands with ; (run all regardless of success)
fn pipe(commands): string  // Join commands with | (pipe output)
fn run(command): bool  // Run a command and return success status
fn run_capture(command): object  // Run a command and capture output
fn run_output(command): string  // Run a command and return stdout, or throw on failure
fn run_lines(command): array  // Run a command and return stdout lines as array
fn env_or(name: string, default_val: string): string  // Get environment variable or default
fn has_env(name: string): bool  // Check if environment variable is set
fn set_envs(vars)  // Set multiple environment variables
fn which(command: string)  // Check if a command exists in PATH
fn command_exists(command: string): bool  // Check if a command exists
fn parse_env_output(output: string): object  // Parse key=value output (one per line)
fn parse_columns(line: string): array  // Parse whitespace-separated columns
fn parse_table(output: string): array  // Parse table output (header + rows)
fn pwd(): string  // Get current working directory
fn ls(path?: "."): array  // List directory contents
fn file_exists(path: string): bool  // Check if file exists
fn dir_exists(path: string): bool  // Check if directory exists
fn mkdir(path: string): bool  // Create directory (with parents)
fn rm(path: string, recursive?: false): bool  // Remove file or directory
fn cp(src: string, dest: string, recursive?: false): bool  // Copy file or directory
fn mv(src: string, dest: string): bool  // Move file or directory
fn subshell(command: string): string  // Run command in subshell
fn background(command: string): string  // Build background command
fn nohup(command: string, output?: "/dev/null"): string  // Build nohup command
fn redirect_stdout(command: string, file: string): string  // Redirect stdout
fn redirect_stderr(command: string, file: string): string  // Redirect stderr
fn redirect_all(command: string, file: string): string  // Redirect both stdout and stderr
fn exec_safe(argv: array): object  // Execute command safely without shell interpretation
fn run_capture_safe(argv: array): object  // Run a command safely and return structured result with proper stderr capture
fn run_output_safe(argv: array): string  // Run a command safely and return stdout, or throw on failure
fn run_safe(argv: array): bool  // Run a command safely and return success status
```

## @stdlib/signal

```
fn signal(signum, handler)  [native]  // signal(signum: i32, handler: fn()) -> null  - Register a signal handler
fn raise(signum)  [native]
let SIGINT  [native]  // Interrupt (Ctrl+C)
let SIGTERM  [native]  // Termination request
let SIGHUP  [native]  // Hangup (terminal closed)
let SIGQUIT  [native]  // Quit (Ctrl+\)
let SIGABRT  [native]  // Abort
let SIGUSR1  [native]  // User-defined signal 1
let SIGUSR2  [native]  // User-defined signal 2
let SIGALRM  [native]  // Alarm timer expired
let SIGCHLD  [native]  // Child process status changed
let SIGPIPE  [native]  // Broken pipe
let SIGCONT  [native]  // Continue if stopped
let SIGSTOP  [native]  // Stop process (cannot be caught)
let SIGTSTP  [native]  // Terminal stop (Ctrl+Z)
let SIGTTIN  [native]  // Background read from terminal
let SIGTTOU  [native]  // Background write to terminal
fn on_shutdown(handler)  // Register a handler for common shutdown signals (SIGINT, SIGTERM, SIGHUP)
fn ignore(signum: i32)  // Ignore a signal
fn reset(signum: i32)  // Reset a signal to its default behavior
fn name(signum: i32): string  // Get the name of a signal number
```

## @stdlib/sqlite

```
let SQLITE_OK = 0  // Result codes
let SQLITE_ERROR = 1
let SQLITE_INTERNAL = 2
let SQLITE_PERM = 3
let SQLITE_ABORT = 4
let SQLITE_BUSY = 5
let SQLITE_LOCKED = 6
let SQLITE_NOMEM = 7
let SQLITE_READONLY = 8
let SQLITE_INTERRUPT = 9
let SQLITE_IOERR = 10
let SQLITE_CORRUPT = 11
let SQLITE_NOTFOUND = 12
let SQLITE_FULL = 13
let SQLITE_CANTOPEN = 14
let SQLITE_PROTOCOL = 15
let SQLITE_EMPTY = 16
let SQLITE_SCHEMA = 17
let SQLITE_TOOBIG = 18
let SQLITE_CONSTRAINT = 19
let SQLITE_MISMATCH = 20
let SQLITE_MISUSE = 21
let SQLITE_NOLFS = 22
let SQLITE_AUTH = 23
let SQLITE_FORMAT = 24
let SQLITE_RANGE = 25
let SQLITE_NOTADB = 26
let SQLITE_NOTICE = 27
let SQLITE_WARNING = 28
let SQLITE_ROW = 100
let SQLITE_DONE = 101
let SQLITE_INTEGER = 1  // Column types
let SQLITE_FLOAT = 2
let SQLITE_TEXT = 3
let SQLITE_BLOB = 4
let SQLITE_NULL = 5
let SQLITE_OPEN_READONLY = 1  // Open flags
let SQLITE_OPEN_READWRITE = 2
let SQLITE_OPEN_CREATE = 4
let SQLITE_OPEN_NOMUTEX = 32768  // 0x00008000
let SQLITE_OPEN_FULLMUTEX = 65536  // 0x00010000
fn sqlite_version(): string  // Get SQLite library version
fn sqlite_version_number(): i32  // Get SQLite library version number
fn open_db(path: string): Database  // Open a database connection.
fn open_db_flags(path: string, flags: i32): Database  // Open a database with flags
fn close_db(db: Database): null  // Close a database connection
fn db_error(db: Database): string  // Get the last error message
fn db_error_code(db: Database): i32  // Get the last error code
fn last_insert_id(db: Database): i64  // Get the rowid of the last inserted row
fn changes(db: Database): i32  // Get the number of rows changed by the last statement
fn total_changes(db: Database): i32  // Get total changes since connection opened
fn exec(db: Database, sql: string, params?: null): null  // Execute SQL statement without returning results
fn query(db: Database, sql: string, params?: null): array  // Execute SQL query and return all results as array of objects
fn query_one(db: Database, sql: string, params?: null)  // Execute SQL query and return first row only (or null if no results)
fn query_value(db: Database, sql: string, params?: null)  // Execute SQL query and return a single value from first column of first row
fn begin(db: Database): null  // Begin a transaction
fn begin_immediate(db: Database): null  // Begin an immediate transaction (acquires write lock immediately)
fn begin_exclusive(db: Database): null  // Begin an exclusive transaction (acquires exclusive lock)
fn commit(db: Database): null  // Commit current transaction
fn rollback(db: Database): null  // Rollback current transaction
fn transaction(db: Database, fn_callback)  // Execute a function within a transaction
fn prepare(db: Database, sql: string): Statement  // Prepare a SQL statement for later execution
fn stmt_bind(stmt: Statement, params: array): null  // Bind parameters to a prepared statement
fn stmt_exec(stmt: Statement, params?: null): null  // Execute a prepared statement (for non-SELECT statements)
fn stmt_query(stmt: Statement, params?: null): array  // Execute a prepared statement and return results (for SELECT statements)
fn stmt_finalize(stmt: Statement): null  // Finalize a prepared statement (release resources)
fn table_exists(db: Database, table_name: string): bool  // Check if a table exists
fn list_tables(db: Database): array  // Get list of tables in the database
fn table_info(db: Database, table_name: string): array  // Get column information for a table
fn memory_db(): Database  // Create an in-memory database
fn insert(db: Database, sql: string, params?: null): i64  // Insert a row and return the last insert rowid
fn update(db: Database, sql: string, params?: null): i32  // Update rows and return number of changes
fn delete_rows(db: Database, sql: string, params?: null): i32  // Delete rows and return number of changes
fn count(db: Database, table_name: string, where_clause?: null): i64  // Count rows in a table
```

## @stdlib/strings

```
fn pad_left(str, width, fill?: " "): string  // Pad string on the left to reach target width
fn pad_right(str, width, fill?: " "): string  // Pad string on the right to reach target width
fn center(str, width, fill?: " "): string  // Center string within target width
fn is_alpha(str): bool  // Check if string contains only alphabetic characters
fn is_digit(str): bool  // Check if string contains only digit characters
fn is_alnum(str): bool  // Check if string contains only alphanumeric characters
fn is_whitespace(str): bool  // Check if string contains only whitespace characters
fn reverse(str): string  // Reverse a string (works with UTF-8 / Unicode codepoints)
fn lines(str): array  // Split string into lines by newline characters
fn words(str): array  // Split string into words by whitespace
fn snake_case(str): string  // Convert string to snake_case
fn camel_case(str): string  // Convert string to camelCase
fn pascal_case(str): string  // Convert string to PascalCase (UpperCamelCase)
fn kebab_case(str): string  // Convert string to kebab-case
fn slugify(str): string  // Convert string to URL-friendly slug
fn truncate(str, max_len, suffix?: "..."): string  // Truncate string to maximum length with optional suffix
fn string_concat_many(...)  [native]
fn from_bytes(src): string  // Build a string from raw bytes
```

## @stdlib/terminal

```
let BLACK  // Text colors
let RED
let GREEN
let YELLOW
let BLUE
let MAGENTA
let CYAN
let WHITE
let GRAY
let BRIGHT_RED
let BRIGHT_GREEN
let BRIGHT_YELLOW
let BRIGHT_BLUE
let BRIGHT_MAGENTA
let BRIGHT_CYAN
let BRIGHT_WHITE
let BG_BLACK  // Background colors
let BG_RED
let BG_GREEN
let BG_YELLOW
let BG_BLUE
let BG_MAGENTA
let BG_CYAN
let BG_WHITE
let BG_GRAY
let BG_BRIGHT_RED
let BG_BRIGHT_GREEN
let BG_BRIGHT_YELLOW
let BG_BRIGHT_BLUE
let BG_BRIGHT_MAGENTA
let BG_BRIGHT_CYAN
let BG_BRIGHT_WHITE
let RESET  // Text styles
let BOLD
let DIM
let ITALIC
let UNDERLINE
let BLINK
let REVERSE
let HIDDEN
let STRIKETHROUGH
let RESET_BOLD  // Reset specific styles
let RESET_ITALIC
let RESET_UNDERLINE
let RESET_BLINK
let RESET_REVERSE
let RESET_HIDDEN
fn move_to(row: i32, col: i32): string  // Move cursor to position (1-indexed)
fn move_up(n: i32): string  // Move cursor up by n lines
fn move_down(n: i32): string  // Move cursor down by n lines
fn move_right(n: i32): string  // Move cursor forward (right) by n columns
fn move_left(n: i32): string  // Move cursor backward (left) by n columns
let SAVE_CURSOR  // Save cursor position
let SAVE_CURSOR_DEC  // DEC save cursor
let RESTORE_CURSOR  // Restore cursor position
let RESTORE_CURSOR_DEC  // DEC restore cursor
let HIDE_CURSOR  // Hide/show cursor
let SHOW_CURSOR
let CLEAR_SCREEN  // Clear entire screen
let CLEAR_TO_END  // Clear from cursor to end of screen
let CLEAR_TO_START  // Clear from cursor to beginning of screen
let CLEAR_LINE  // Clear entire line
let CLEAR_LINE_TO_END  // Clear from cursor to end of line
let CLEAR_LINE_TO_START  // Clear from cursor to start of line
fn scroll_up(n: i32): string  // Scroll up by n lines
fn scroll_down(n: i32): string  // Scroll down by n lines
fn color(text: string, code: string): string  // Wrap text with color
fn color_bg(text: string, fg: string, bg: string): string  // Wrap text with foreground and background color
fn rgb(r: i32, g: i32, b: i32): string  // RGB color support (24-bit true color)
fn bg_rgb(r: i32, g: i32, b: i32): string
fn color_256(n: i32): string  // 256-color palette support
fn bg_color_256(n: i32): string
fn size()  // Get terminal size (TIOCGWINSZ on POSIX, console API on Windows)
fn supports_color(): bool  // Check if terminal supports colors
fn ProgressBar(total: i32, width?: 40)  // Progress bar with percentage
fn Spinner(frames?: null)  // Spinner animation
fn SPINNER_DOTS()  // Predefined spinner styles
fn SPINNER_LINE()
fn SPINNER_ARROW()
fn SPINNER_BOUNCE()
fn SPINNER_CLOCK()
fn clear()  // Clear screen and move cursor to top-left
fn print_at(row: i32, col: i32, text: string)  // Print text at specific position
fn print_color(text: string, code: string)  // Print colored text
fn print_styled(text: string, style: string)  // Print styled text (bold, underline, etc.)
```

## @stdlib/termios

```
let KEY_NONE = 0
let KEY_UP = 256
let KEY_DOWN = 257
let KEY_RIGHT = 258
let KEY_LEFT = 259
let KEY_HOME = 260
let KEY_END = 261
let KEY_INSERT = 262
let KEY_DELETE = 263
let KEY_PAGE_UP = 264
let KEY_PAGE_DOWN = 265
let KEY_F1 = 266
let KEY_F2 = 267
let KEY_F3 = 268
let KEY_F4 = 269
let KEY_F5 = 270
let KEY_F6 = 271
let KEY_F7 = 272
let KEY_F8 = 273
let KEY_F9 = 274
let KEY_F10 = 275
let KEY_F11 = 276
let KEY_F12 = 277
let KEY_ESCAPE = 27
let KEY_ENTER = 13
let KEY_TAB = 9
let KEY_BACKSPACE = 127
let KEY_CTRL_C = 3
let KEY_CTRL_D = 4
let KEY_CTRL_Z = 26
fn is_terminal(): bool  // Check if stdin is connected to a terminal (not a pipe or file)
fn enable_raw_mode(): bool  // Enable raw mode - keypresses are available immediately without Enter
fn disable_raw_mode(): bool  // Disable raw mode - restore original terminal settings
fn is_raw_mode(): bool  // Check if currently in raw mode
fn read_key(): object  // Read a single keypress (blocking)
fn read_key_timeout(timeout_ms: i32): object  // Read a key with timeout (milliseconds)
fn is_key(key: object, code: i32): bool  // Check if a key result is a specific key code
fn is_printable(key: object): bool  // Check if a key result is a printable character
fn is_arrow(key: object): bool  // Check if a key result is an arrow key
fn key_name(key: object): string  // Get key name for display
fn with_raw_mode(callback: fn(): void)  // Execute a function in raw mode, automatically restoring on exit
```

## @stdlib/testing

```

```

## @stdlib/time

```
fn now()  [native]  // Current Unix timestamp (seconds since epoch) as i64
fn time_ms()  [native]  // Current time in milliseconds as i64
fn clock()  [native]  // CPU time used by process in seconds as f64
fn sleep(seconds)  [native]  // Sleep for specified seconds (accepts f64 for sub-second precision)
```

## @stdlib/toml

```
fn parse(input: string): object  // Parse a TOML string into an object
fn stringify(obj): string  // Convert an object to TOML string
fn parse_file(path: string): object  // Parse a TOML file
fn write_file(path: string, obj)  // Write object to TOML file
fn get(obj, path: string)  // Get a value using a dotted key path
fn set(obj, path: string, value)  // Set a value using a dotted key path
```

## @stdlib/unix_socket

```
let AF_UNIX  [native]
let SOCK_STREAM  [native]
let SOCK_DGRAM  [native]
fn UnixListener(path: string)  // UnixListener(path: string) -> UnixListener object
fn UnixStream(path: string)  // UnixStream(path: string) -> UnixStream object
fn UnixDgramSocket(path: string)  // UnixDgramSocket(path: string) -> UnixDgramSocket object
fn remove_socket(path: string)  // remove_socket(path: string) -> null
```

## @stdlib/url

```
fn parse(url): object  // Parse a URL string into its components
fn format(url_obj): string  // Format a URL object back to a string
fn parse_query(query): array  // Parse a query string into an array of {name, value} pairs
fn format_query(params): string  // Format an array of {name, value} pairs into a query string
fn get_query_param(url, name)  // Get a query parameter value from a URL
fn get_query_param_all(url, name): array  // Get all values for a query parameter (for repeated params)
fn set_query_param(url, name, value): string  // Set a query parameter value in a URL
fn remove_query_param(url, name): string  // Remove a query parameter from a URL
fn encode_component(str): string  // Encode a URI component (similar to JavaScript's encodeURIComponent)
fn decode_component(str): string  // Decode a URI component (similar to JavaScript's decodeURIComponent)
fn resolve(base, relative): string  // Resolve a relative URL against a base URL
fn join(base, path): string  // Join a base URL with a path
fn is_absolute(url): bool  // Check if a URL is absolute
fn get_origin(url): string  // Get the origin of a URL (scheme + host + port)
fn get_path_query(url): string  // Get the path and query string
fn is_valid(url): bool  // Check if a string is a valid URL
fn normalize(url): string  // Normalize a URL (lowercase scheme and host, resolve path)
```

## @stdlib/uuid

```
fn v4(): string  // Generate a UUID v4 (random)
fn v7(): string  // Generate a UUID v7 (time-ordered, sortable)
fn parse(uuid): object  // Parse a UUID string into its components
fn is_valid(uuid): bool  // Check if a string is a valid UUID
fn compare(a, b): i32  // Compare two UUIDs
fn equals(a, b): bool  // Check if two UUIDs are equal (case-insensitive)
fn to_upper(uuid): string  // Convert UUID to uppercase format
fn to_lower(uuid): string  // Convert UUID to lowercase format
let NIL = "00000000-0000-0000-0000-000000000000"  // The nil UUID (all zeros)
fn is_nil(uuid): bool  // Check if a UUID is the nil UUID
fn v1(): string  // Generate a UUID v1 (time-based)
fn short_id(): string  // Generate a short ID (first 8 characters of a v4 UUID)
fn compact(): string  // Generate a compact UUID (no hyphens)
```

## @stdlib/vector

```
let METRIC_UNKNOWN = 0
let METRIC_COSINE = 1
let METRIC_IP = 2  // Inner product (dot product)
let METRIC_L2SQ = 3  // Euclidean (L2 squared)
let METRIC_HAVERSINE = 4  // Geographic distance
let METRIC_DIVERGENCE = 5  // Jensen-Shannon divergence
let METRIC_PEARSON = 6  // Pearson correlation
let METRIC_JACCARD = 7  // Jaccard similarity
let METRIC_HAMMING = 8  // Hamming distance
let METRIC_TANIMOTO = 9  // Tanimoto coefficient
let METRIC_SORENSEN = 10  // Sorensen-Dice coefficient
let SCALAR_UNKNOWN = 0
let SCALAR_F32 = 1
let SCALAR_F64 = 2
let SCALAR_F16 = 3
let SCALAR_I8 = 4
let SCALAR_B1 = 5  // Binary (1-bit)
let SCALAR_BF16 = 6  // Brain float 16
fn version(): string  // Get USearch library version string
fn create_index(dimensions: i64, metric?: 1, quantization?: 1, connectivity?: 0, expansion_add?: 0, expansion_search?: 0, multi?: false): VectorIndex  // Create a new vector index
fn load_index(path: string, dimensions?: 0, metric?: 0, quantization?: 0): VectorIndex  // Load a previously saved index from disk
fn view_index(path: string, dimensions?: 0, metric?: 0, quantization?: 0): VectorIndex  // Open a memory-mapped read-only view of an index file
fn index_metadata(path: string): object  // Read index metadata from a file without loading the full index
fn free_index(idx: VectorIndex): null  // Free index resources
fn add(idx: VectorIndex, key: i64, vector: array, scalar?: 1): null  // Add a vector to the index
fn add_batch(idx: VectorIndex, keys: array, vectors: array, scalar?: 1): null  // Add multiple vectors in batch
fn search(idx: VectorIndex, query: array, k?: 10, scalar?: 1): array  // Search for k nearest neighbors
fn search_filtered(idx: VectorIndex, query: array, k: i64, filter, scalar?: 1): array  // Search for k nearest neighbors with a filter predicate
fn search_batch(idx: VectorIndex, queries: array, k?: 10, scalar?: 1): array  // Batch search for k nearest neighbors of multiple queries
fn remove(idx: VectorIndex, key: i64): i64  // Remove a vector by key
fn rename(idx: VectorIndex, from_key: i64, to_key: i64): i64  // Rename a vector's key
fn contains(idx: VectorIndex, key: i64): bool  // Check if a key exists in the index
fn count(idx: VectorIndex, key: i64): i64  // Count vectors for a key (useful for multi-indexes where multiple vectors share a key)
fn get(idx: VectorIndex, key: i64, scalar?: 1)  // Retrieve a stored vector by key
fn save(idx: VectorIndex, path: string): null  // Save index to disk
fn size(idx: VectorIndex): i64  // Get number of vectors in the index
fn capacity(idx: VectorIndex): i64  // Get allocated capacity
fn dimensions(idx: VectorIndex): i64  // Get number of dimensions
fn connectivity(idx: VectorIndex): i64  // Get HNSW graph connectivity (M parameter)
fn memory_usage(idx: VectorIndex): i64  // Get current memory usage in bytes
fn serialized_length(idx: VectorIndex): i64  // Get serialized length in bytes (how large the file would be)
fn hardware_acceleration(idx: VectorIndex): string  // Get hardware acceleration info (e.g., "avx512", "neon", "serial")
fn get_expansion_add(idx: VectorIndex): i64  // Get current expansion factor for insertion
fn get_expansion_search(idx: VectorIndex): i64  // Get current expansion factor for search
fn set_expansion_add(idx: VectorIndex, expansion: i64): null  // Set expansion factor for insertion (ef_construction)
fn set_expansion_search(idx: VectorIndex, expansion: i64): null  // Set expansion factor for search (ef)
fn set_threads_add(idx: VectorIndex, threads: i64): null  // Set number of threads for parallel insertion
fn set_threads_search(idx: VectorIndex, threads: i64): null  // Set number of threads for parallel search
fn set_metric(idx: VectorIndex, metric: i32): null  // Change the distance metric to a built-in metric kind
fn set_custom_metric(idx: VectorIndex, metric_fn, metric_kind?: 0): ptr  // Set a user-defined distance metric function
fn free_custom_metric(metric_cb: ptr): null  // Free a custom metric callback created by set_custom_metric()
fn reserve(idx: VectorIndex, cap: i64): null  // Pre-allocate capacity for a known number of vectors
fn clear(idx: VectorIndex): null  // Remove all vectors from the index
fn distance(a: array, b: array, metric?: 1, scalar?: 1): f32  // Compute the distance between two vectors without an index
fn exact_search(dataset: array, queries: array, k?: 10, metric?: 1, threads?: 1, scalar?: 1): array  // Perform exact (brute-force) k-NN search over a dataset without building an index
```

## @stdlib/websocket

```
fn WebSocket(url: string)
fn WebSocketServer(host: string, port: i32)
fn is_secure_url(url: string): bool
fn parse_ws_url(url: string)
```

## @stdlib/yaml

```
fn parse(input: string)  // Parse a YAML string into a value (first document)
fn parse_all(input: string): array  // Parse all documents from a multi-document YAML string
fn stringify(value, indent?: 2): string  // Convert a value to a YAML string
fn parse_file(path: string)  // Parse a YAML file
fn write_file(path: string, value)  // Write value to a YAML file
fn get(obj, path: string)  // Get a value using a dotted key path
fn set(obj, path: string, value)  // Set a value using a dotted key path
```
