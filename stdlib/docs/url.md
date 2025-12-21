# @stdlib/url - URL Parsing and Manipulation

The `url` module provides functions for parsing, building, and manipulating URLs.

## Quick Start

```hemlock
import { parse, format, get_query_param, resolve } from "@stdlib/url";

// Parse a URL
let u = parse("https://example.com:8080/path?foo=bar#section");
print(u["host"]);   // "example.com"
print(u["port"]);   // "8080"
print(u["query"]);  // "foo=bar"

// Get query parameter
let val = get_query_param("https://example.com?name=Alice", "name");
print(val);  // "Alice"

// Resolve relative URL
let abs = resolve("https://example.com/path/page", "../other");
print(abs);  // "https://example.com/other"
```

## API Reference

### parse(url): object

Parse a URL string into its components.

**Returns:** `{ scheme, username, password, host, port, path, query, fragment, href }`

```hemlock
import { parse } from "@stdlib/url";

let u = parse("https://user:pass@example.com:8080/path?q=test#section");
print(u["scheme"]);    // "https"
print(u["username"]);  // "user"
print(u["password"]);  // "pass"
print(u["host"]);      // "example.com"
print(u["port"]);      // "8080"
print(u["path"]);      // "/path"
print(u["query"]);     // "q=test"
print(u["fragment"]);  // "section"
```

### format(url_obj): string

Format a URL object back to a string.

```hemlock
import { format } from "@stdlib/url";

let url = format({
    scheme: "https",
    host: "example.com",
    path: "/api/v1",
    query: "key=value"
});
print(url);  // "https://example.com/api/v1?key=value"
```

### parse_query(query): array

Parse a query string into an array of `{name, value}` objects.

```hemlock
import { parse_query } from "@stdlib/url";

let params = parse_query("name=Alice&age=30");
print(params[0]["name"]);   // "name"
print(params[0]["value"]);  // "Alice"
```

### format_query(params): string

Format an array of `{name, value}` objects into a query string.

```hemlock
import { format_query } from "@stdlib/url";

let query = format_query([
    { name: "foo", value: "bar" },
    { name: "baz", value: "qux" }
]);
print(query);  // "foo=bar&baz=qux"
```

### get_query_param(url, name): string|null

Get a query parameter value from a URL.

```hemlock
import { get_query_param } from "@stdlib/url";

let url = "https://example.com/search?q=hello&lang=en";
print(get_query_param(url, "q"));     // "hello"
print(get_query_param(url, "lang"));  // "en"

let missing = get_query_param(url, "foo");
if (missing == null) {
    print("Parameter not found");
}
```

### get_query_param_all(url, name): array

Get all values for a repeated query parameter.

```hemlock
import { get_query_param_all } from "@stdlib/url";

let url = "https://example.com/?tag=js&tag=web&tag=dev";
let tags = get_query_param_all(url, "tag");
print(tags.join(", "));  // "js, web, dev"
```

### set_query_param(url, name, value): string

Set or update a query parameter in a URL.

```hemlock
import { set_query_param } from "@stdlib/url";

let url = set_query_param("https://example.com/page?a=1", "b", "2");
print(url);  // "https://example.com/page?a=1&b=2"

// Update existing parameter
let updated = set_query_param(url, "a", "new");
print(updated);  // "https://example.com/page?a=new&b=2"
```

### remove_query_param(url, name): string

Remove a query parameter from a URL.

```hemlock
import { remove_query_param } from "@stdlib/url";

let url = remove_query_param("https://example.com?a=1&b=2&c=3", "b");
print(url);  // "https://example.com?a=1&c=3"
```

### encode_component(str): string

Encode a string for use in a URL (percent-encoding).

```hemlock
import { encode_component } from "@stdlib/url";

print(encode_component("Hello World!"));  // "Hello%20World%21"
print(encode_component("a=b&c=d"));       // "a%3Db%26c%3Dd"
```

### decode_component(str): string

Decode a percent-encoded string.

```hemlock
import { decode_component } from "@stdlib/url";

print(decode_component("Hello%20World%21"));  // "Hello World!"
print(decode_component("foo+bar"));           // "foo bar"
```

### resolve(base, relative): string

Resolve a relative URL against a base URL.

```hemlock
import { resolve } from "@stdlib/url";

let base = "https://example.com/path/to/page";

// Already absolute - returned as-is
print(resolve(base, "https://other.com/new"));
// "https://other.com/new"

// Protocol-relative
print(resolve(base, "//cdn.example.com/resource"));
// "https://cdn.example.com/resource"

// Absolute path
print(resolve(base, "/new/path"));
// "https://example.com/new/path"

// Relative path
print(resolve(base, "sibling"));
// "https://example.com/path/to/sibling"

// Parent directory
print(resolve(base, "../other"));
// "https://example.com/path/other"

// Query only
print(resolve(base, "?newquery=1"));
// "https://example.com/path/to/page?newquery=1"

// Fragment only
print(resolve(base, "#section"));
// "https://example.com/path/to/page#section"
```

### join(base, path): string

Join a base URL with a path.

```hemlock
import { join } from "@stdlib/url";

print(join("https://api.example.com", "v1/users"));
// "https://api.example.com/v1/users"

// Handles extra slashes
print(join("https://api.example.com/", "/v1/users"));
// "https://api.example.com/v1/users"
```

### is_absolute(url): bool

Check if a URL is absolute (has a scheme).

```hemlock
import { is_absolute } from "@stdlib/url";

print(is_absolute("https://example.com"));  // true
print(is_absolute("/path/to/file"));         // false
print(is_absolute("relative/path"));         // false
```

### get_origin(url): string

Get the origin of a URL (scheme + host + port).

```hemlock
import { get_origin } from "@stdlib/url";

print(get_origin("https://user:pass@example.com:8080/path?query"));
// "https://example.com:8080"
```

### get_path_query(url): string

Get the path, query string, and fragment from a URL.

```hemlock
import { get_path_query } from "@stdlib/url";

print(get_path_query("https://example.com/path?query=1#section"));
// "/path?query=1#section"
```

### is_valid(url): bool

Check if a string is a valid URL.

```hemlock
import { is_valid } from "@stdlib/url";

print(is_valid("https://example.com"));  // true
print(is_valid("not-a-url"));            // false
print(is_valid("http://"));              // false
```

### normalize(url): string

Normalize a URL (lowercase scheme/host, resolve path, remove default ports).

```hemlock
import { normalize } from "@stdlib/url";

print(normalize("HTTPS://EXAMPLE.COM:443/path/../other/./file"));
// "https://example.com/other/file"

print(normalize("https://example.com:8080/path"));
// "https://example.com:8080/path" (non-default port kept)
```

## Examples

### Building API URLs

```hemlock
import { join, set_query_param } from "@stdlib/url";

let base = "https://api.example.com/v1";
let endpoint = join(base, "users");
let url = set_query_param(endpoint, "limit", "10");
url = set_query_param(url, "offset", "20");
print(url);
// "https://api.example.com/v1/users?limit=10&offset=20"
```

### Parsing and modifying URLs

```hemlock
import { parse, format } from "@stdlib/url";

let u = parse("https://example.com/path?old=value");

// Modify components
let new_url = format({
    scheme: u["scheme"],
    host: u["host"],
    path: "/new/path",
    query: "new=value"
});
print(new_url);  // "https://example.com/new/path?new=value"
```

### Working with query parameters

```hemlock
import { parse_query, format_query, encode_component } from "@stdlib/url";

// Parse and filter
let params = parse_query("a=1&b=2&c=3");
let filtered: array = [];
let i = 0;
while (i < params.length) {
    if (params[i]["name"] != "b") {
        filtered.push(params[i]);
    }
    i = i + 1;
}

// Add new parameter with special characters
filtered.push({ name: "msg", value: "Hello World!" });

print(format_query(filtered));
// "a=1&c=3&msg=Hello%20World%21"
```

## See Also

- [@stdlib/encoding](encoding.md) - URL encoding utilities (url_encode, url_decode)
- [@stdlib/http](http.md) - HTTP client functions
