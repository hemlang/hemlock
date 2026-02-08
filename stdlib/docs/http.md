# @stdlib/http - HTTP Client Module

Production-ready HTTP client module using libcurl.

## Overview

The `@stdlib/http` module provides a battle-tested HTTP/HTTPS client by wrapping the curl CLI tool via `exec()`. This pragmatic approach delivers production-quality HTTP functionality with minimal complexity.

**Implementation:**
- Wraps curl CLI via `exec()` for simplicity and reliability
- FFI declarations to libcurl included (for future direct FFI implementation)
- Supports HTTP and HTTPS (via curl's OpenSSL support)
- Handles redirects, all HTTP methods, custom headers
- Production-ready: uses the same curl that powers millions of applications

**Why exec() wrapper instead of direct FFI?**
- Simpler implementation (no callback complexity)
- Immediate HTTPS support (curl handles TLS/SSL)
- Proven reliability (curl is battle-tested)
- 90% of functionality with 10% of complexity
- Future: can add direct FFI for performance-critical use cases

## Installation

Requires curl to be installed:
```bash
# Ubuntu/Debian
apt-get install curl

# Most systems have curl pre-installed
which curl
```

## Import

```hemlock
import { get, post, fetch } from "@stdlib/http";
```

## API Reference

### HTTP Methods

#### `get(url: string, headers?: array<string>): object`

Perform an HTTP GET request.

```hemlock
import { get } from "@stdlib/http";

// Simple GET
let response = get("https://api.github.com/users/octocat", null);
print(response.status_code);  // 200
print(response.body);          // JSON response

// With custom headers
let headers = [
    "Authorization: Bearer token123",
    "Accept: application/json"
];
let response = get("https://api.example.com/users", headers);
```

**Returns:**
```hemlock
{
    status_code: i32,    // HTTP status code (200, 404, etc.)
    headers: string,     // Response headers (currently empty)
    body: string,        // Response body
}
```

#### `get_binary(url: string, headers?: array<string>): object`

Perform an HTTP GET request for binary data (images, files, etc.).

```hemlock
import { get_binary } from "@stdlib/http";

// Download binary content
let response = get_binary("https://example.com/image.png", null);
if (response.status_code == 200) {
    // response.body contains raw binary data
    print(`Downloaded ${response.body.length} bytes`);
}
```

**Returns:** Same structure as `get()`, but body may contain binary data.

#### `post(url: string, body?: string, headers?: array<string>): object`

Perform an HTTP POST request.

```hemlock
import { post } from "@stdlib/http";

let body = '{"name":"Alice","age":30}';
let headers = ["Content-Type: application/json"];
let response = post("https://httpbin.org/post", body, headers);
print(response.body);
```

#### `put(url: string, body?: string, headers?: array<string>): object`

Perform an HTTP PUT request.

```hemlock
let body = '{"name":"Bob"}';
let response = put("https://api.example.com/users/1", body, null);
```

#### `delete(url: string, headers?: array<string>): object`

Perform an HTTP DELETE request.

```hemlock
let response = delete("https://api.example.com/users/1", null);
```

#### `request(method: string, url: string, body?: string, headers?: array<string>): object`

Perform a generic HTTP request with any method.

```hemlock
let response = request("PATCH", "https://api.example.com/users/1", '{"name":"Charlie"}', null);
```

### Convenience Functions

#### `fetch(url: string): string`

Fetch a URL and return just the body as a string.

```hemlock
import { fetch } from "@stdlib/http";

let html = fetch("https://example.com");
print(html);
```

#### `post_json(url: string, data: object): object`

POST a Hemlock object as JSON (automatically serializes and sets Content-Type).

```hemlock
import { post_json } from "@stdlib/http";

let user = { name: "Alice", age: 30, active: true };
let response = post_json("https://httpbin.org/post", user);
print(response.body);
```

#### `get_json(url: string): object`

GET a URL and automatically parse the response as JSON.

```hemlock
import { get_json } from "@stdlib/http";

let user = get_json("https://jsonplaceholder.typicode.com/users/1");
print(user.name);  // "Leanne Graham"
print(user.email); // "Sincere@april.biz"
```

#### `download(url: string, output_path: string): bool`

Download a file from a URL and save it to disk.

```hemlock
import { download } from "@stdlib/http";

let success = download("https://example.com/file.pdf", "/tmp/file.pdf");
if (success) {
    print("Downloaded successfully");
}
```

### Status Code Helpers

#### `is_success(status_code: i32): bool`

Check if status code indicates success (200-299).

```hemlock
import { get, is_success } from "@stdlib/http";

let response = get("https://example.com", null);
if (is_success(response.status_code)) {
    print("Success!");
}
```

#### `is_redirect(status_code: i32): bool`

Check if status code indicates redirect (300-399).

#### `is_client_error(status_code: i32): bool`

Check if status code indicates client error (400-499).

#### `is_server_error(status_code: i32): bool`

Check if status code indicates server error (500-599).

### Timeout Functions

All HTTP functions have corresponding `*_timeout` versions that accept a custom timeout in milliseconds. The default timeout is 30000ms (30 seconds).

#### `get_timeout(url: string, headers?: array<string>, timeout_ms?: i32): object`

GET request with custom timeout.

```hemlock
import { get_timeout } from "@stdlib/http";

// 60 second timeout for slow APIs
let response = get_timeout("https://slow-api.example.com/data", null, 60000);
```

#### `post_timeout(url: string, body?: string, headers?: array<string>, timeout_ms?: i32): object`

POST request with custom timeout.

```hemlock
import { post_timeout } from "@stdlib/http";

let response = post_timeout("https://api.example.com/upload", large_body, null, 120000);
```

#### `request_timeout(method: string, url: string, body?: string, headers?: array<string>, timeout_ms?: i32): object`

Generic HTTP request with custom timeout.

```hemlock
import { request_timeout } from "@stdlib/http";

let response = request_timeout("PUT", "https://api.example.com/data", body, headers, 45000);
```

#### `post_json_timeout(url: string, data: object, timeout_ms?: i32): object`

POST JSON with custom timeout. Ideal for LLM APIs that may take longer to respond.

```hemlock
import { post_json_timeout } from "@stdlib/http";

// OpenAI API may take 60+ seconds for large prompts
let response = post_json_timeout(
    "https://api.openai.com/v1/chat/completions",
    {
        model: "gpt-4",
        messages: [{ role: "user", content: prompt }]
    },
    60000  // 60 second timeout
);
```

### URL Helpers

#### `url_encode(str: string): string`

Encode a string for use in URLs.

```hemlock
import { url_encode } from "@stdlib/http";

let encoded = url_encode("hello world!");  // "hello%20world%21"
let url = "https://api.example.com/search?q=" + url_encode("foo & bar");
```

## Examples

### Basic GET Request

```hemlock
import { get } from "@stdlib/http";

let response = get("https://httpbin.org/get", null);
print(`Status: ${response.status_code}`);
print("Body: " + response.body);
```

### POST JSON Data

```hemlock
import { post_json } from "@stdlib/http";

let data = {
    title: "Buy groceries",
    completed: false,
    userId: 1
};

let response = post_json("https://jsonplaceholder.typicode.com/todos", data);
print(response.body);
```

### Custom Headers & Authentication

```hemlock
import { get } from "@stdlib/http";

let headers = [
    "User-Agent: My-App/1.0",
    "Accept: application/json",
    "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
];

let response = get("https://api.example.com/protected", headers);
```

### Error Handling

```hemlock
import { get, is_success, is_client_error } from "@stdlib/http";

try {
    let response = get("https://httpbin.org/status/404", null);

    if (is_success(response.status_code)) {
        print("Success: " + response.body);
    } else if (is_client_error(response.status_code)) {
        print(`Client error: HTTP ${response.status_code}`);
    } else {
        print(`Error: HTTP ${response.status_code}`);
    }
} catch (e) {
    print("Request failed: " + e);
}
```

### Fetch and Parse JSON API

```hemlock
import { get_json } from "@stdlib/http";

// Fetch GitHub user data
let user = get_json("https://api.github.com/users/octocat");
print("Name: " + user.name);
print("Bio: " + user.bio);
print(`Public repos: ${user.public_repos}`);

// Fetch todos
let todos = get_json("https://jsonplaceholder.typicode.com/todos/1");
print("Title: " + todos.title);
print(`Completed: ${todos.completed}`);
```

### Download File

```hemlock
import { download } from "@stdlib/http";

print("Downloading...");
let success = download("https://httpbin.org/image/png", "/tmp/test.png");

if (success) {
    print("Downloaded to /tmp/test.png");
} else {
    print("Download failed");
}
```

### Multiple Requests

```hemlock
import { get_json } from "@stdlib/http";

// Fetch multiple users
let i = 1;
while (i <= 3) {
    let user = get_json(`https://jsonplaceholder.typicode.com/users/${i}`);
    print(`${i}. ${user.name} (${user.email})`);
    i = i + 1;
}
```

## Features

### Supported

✅ **HTTP and HTTPS** - Full TLS/SSL support via curl
✅ **All HTTP methods** - GET, POST, PUT, DELETE, PATCH, etc.
✅ **Custom headers** - Authorization, Content-Type, etc.
✅ **Request body** - POST/PUT data
✅ **Redirects** - Automatically follows redirects
✅ **JSON support** - Built-in JSON serialization/deserialization
✅ **File downloads** - Save responses to disk
✅ **Error handling** - Exceptions for failures
✅ **Status codes** - Helper functions for code ranges
✅ **URL encoding** - Basic URL encoding support

### Current Limitations

⚠️ **Binary responses** - Text-focused (works for most APIs)
⚠️ **Progress callbacks** - Not supported
⚠️ **Concurrent requests** - Sequential only (use async/spawn for concurrency)
⚠️ **Cookie management** - Not built-in

### Workarounds

**For concurrent requests:**
```hemlock
import { get } from "@stdlib/http";

async fn fetch_url(url: string): object {
    return get(url, null);
}

// Fetch multiple URLs in parallel
let task1 = spawn(fetch_url, "https://api.example.com/data1");
let task2 = spawn(fetch_url, "https://api.example.com/data2");
let task3 = spawn(fetch_url, "https://api.example.com/data3");

let result1 = await task1;
let result2 = await task2;
let result3 = await task3;
```

## Implementation Notes

### Current Implementation: exec() Wrapper

This module wraps the curl CLI tool via Hemlock's `exec()` builtin:

```hemlock
// Simplified internal implementation
let cmd = "curl -s -w '\\n%{http_code}' -L -X POST";
cmd = cmd + " -H 'Content-Type: application/json'";
cmd = cmd + " -d '" + body + "'";
cmd = cmd + " '" + url + "'";

let result = exec(cmd);
// Parse result.output to extract body and status code
```

**Advantages:**
- Simple and reliable
- Full HTTPS/TLS support (via curl's OpenSSL)
- Handles redirects, compression, chunked encoding
- Battle-tested (curl powers millions of apps)
- No complex FFI callback setup required

**Trade-offs:**
- Process spawn overhead (~1-5ms per request)
- Not suitable for extremely high-frequency requests (100s/sec)
- For most use cases (APIs, webhooks, scraping): perfectly fine

### Future: Direct FFI

FFI declarations to libcurl are included in the module for future implementation:

```hemlock
// Already declared (not yet fully implemented)
extern fn curl_easy_init(): ptr;
extern fn curl_easy_setopt(handle: ptr, option: i32, parameter: ptr): i32;
extern fn curl_easy_perform(handle: ptr): i32;
// ...
```

**To implement direct FFI:**
1. Create C wrapper library for write callbacks
2. Handle memory management for curl_slist (headers)
3. Implement response body accumulation
4. Add proper error handling for curl error codes

This would reduce overhead for high-frequency requests but adds significant complexity.

## Dependencies

**Required:**
- curl CLI tool (usually pre-installed)

**Check installation:**
```bash
which curl
curl --version
```

**Install if needed:**
```bash
# Ubuntu/Debian
sudo apt-get install curl

# Fedora/RHEL
sudo yum install curl

# macOS (usually pre-installed)
brew install curl
```

## Streaming HTTP

Hemlock supports streaming HTTP responses for chunked transfer encoding and Server-Sent Events (SSE). This is essential for consuming streaming LLM APIs (OpenAI, Anthropic, etc.) where tokens arrive incrementally.

### `stream(method: string, url: string, body?: string, headers?: array<string>, timeout_ms?: i32): object`

Open a streaming HTTP connection that delivers response data incrementally as chunks arrive.

```hemlock
import { stream } from "@stdlib/http";

let s = stream("GET", "https://example.com/stream", null, null, 60000);
print(`Status: ${s.status_code}`);

while (!s.done) {
    let chunk = s.read(30000);
    if (chunk != null) {
        print(chunk);
    }
}
s.close();
```

**Returns:** A stream object with:
- `status_code: i32` - HTTP status code
- `headers: string` - Response headers
- `done: bool` - Whether the stream is finished
- `read(timeout_ms?: i32): string|null` - Read next chunk (null when done)
- `close()` - Close the connection and free resources

### `stream_get(url: string, headers?: array<string>, timeout_ms?: i32): object`

Convenience function for streaming GET requests.

```hemlock
import { stream_get } from "@stdlib/http";

let s = stream_get("https://example.com/events", null, 60000);
while (!s.done) {
    let chunk = s.read();
    if (chunk != null) { print(chunk); }
}
s.close();
```

### `stream_post(url: string, body?: string, headers?: array<string>, timeout_ms?: i32): object`

Convenience function for streaming POST requests.

### `post_json_stream(url: string, data: object, timeout_ms?: i32): object`

POST JSON data and stream the response. Automatically serializes the data and sets `Content-Type: application/json`. Designed for streaming LLM API responses.

```hemlock
import { post_json_stream } from "@stdlib/http";

// Stream tokens from an LLM API
let s = post_json_stream("https://api.openai.com/v1/chat/completions", {
    model: "gpt-4",
    messages: [{ role: "user", content: "Tell me a story" }],
    stream: true
}, 120000);

if (s.status_code != 200) {
    print("Error: HTTP " + s.status_code);
} else {
    while (!s.done) {
        let chunk = s.read(30000);
        if (chunk != null) {
            // Each chunk is a raw SSE line like: data: {"choices":[...]}
            print(chunk);
        }
    }
}
s.close();
```

### `stream_sse(url: string, headers?: array<string>, timeout_ms?: i32): object`

Stream Server-Sent Events with automatic SSE protocol parsing. Each call to `next_event()` returns a parsed event object.

```hemlock
import { stream_sse } from "@stdlib/http";

let sse = stream_sse("https://api.example.com/events", [
    "Authorization: Bearer token123"
], 120000);

while (!sse.done) {
    let event = sse.next_event();
    if (event != null) {
        print("Type: " + event.event);   // "message", "update", etc.
        print("Data: " + event.data);    // Event payload
        print("ID: " + event.id);        // Event ID (if any)
    }
}
sse.close();
```

**SSE Event object:**
```hemlock
{
    event: string,   // Event type (default: "message")
    data: string,    // Event data (may be multi-line)
    id: string       // Event ID
}
```

**SSE Protocol:** The parser handles the standard SSE format:
```
event: message
data: {"text":"hello"}
id: 123

```

Fields are separated by newlines, events by double newlines. Lines starting with `:` are comments and are ignored.

### Streaming LLM Example (Complete)

```hemlock
import { post_json_stream } from "@stdlib/http";
import { parse } from "@stdlib/json";

let api_key = getenv("OPENAI_API_KEY");

let s = post_json_stream("https://api.openai.com/v1/chat/completions", {
    model: "gpt-4",
    messages: [{ role: "user", content: "Write a haiku about programming" }],
    stream: true
});

while (!s.done) {
    let chunk = s.read(30000);
    if (chunk != null) {
        // Parse SSE data lines
        let lines = chunk.split("\n");
        for (line in lines) {
            if (line.starts_with("data: ") && line != "data: [DONE]") {
                let json_str = line.substr(6, line.length - 6);
                let obj = parse(json_str);
                let content = obj.choices[0].delta.content;
                if (content != null) {
                    print(content);  // Print each token as it arrives
                }
            }
        }
    }
}
s.close();
```

## HTTP Server

#### `HttpServer(host: string, port: i32): object`

Create a simple HTTP server.

```hemlock
import { HttpServer } from "@stdlib/http";

// Create server
let server = HttpServer("0.0.0.0", 8080);

// Handle requests
server.on_request(fn(req) {
    print("Request: " + req.method + " " + req.path);
    return {
        status: 200,
        headers: ["Content-Type: text/plain"],
        body: "Hello, World!"
    };
});

// Start listening
server.start();
```

**Note:** This is an experimental feature. For production use, consider using the `@stdlib/net` module for more control.

## See Also

- `exec()` builtin - Execute shell commands
- `@stdlib/net` - Low-level TCP/UDP sockets
- CLAUDE.md - Standard library documentation
