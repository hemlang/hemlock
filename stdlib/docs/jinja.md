# @stdlib/jinja - Jinja Template Rendering

## Overview

The jinja module provides Jinja2-compatible template rendering for Hemlock. It supports variable interpolation, control flow structures, filters, whitespace control, and more, making it suitable for generating LLM chat templates (like Qwen3) and general text templating.

## Quick Start

```hemlock
import { render, namespace } from "@stdlib/jinja";

// Simple variable substitution
let result = render("Hello, {{ name }}!", { name: "World" });
print(result);  // "Hello, World!"

// With filters
let result2 = render("{{ name|upper }}", { name: "alice" });
print(result2);  // "ALICE"

// Control flow
let template = `
{% for item in items %}
- {{ item }}
{% endfor %}
`;
print(render(template, { items: ["apple", "banana", "cherry"] }));
```

## Syntax Reference

### Variables

Variables are enclosed in double curly braces:

```hemlock
{{ variable }}
{{ object.property }}
{{ array[0] }}
{{ deeply.nested.value }}
```

### Expressions

Arithmetic and string operations in expressions:

```hemlock
{{ 'Hello' + ' ' + name }}      // String concatenation
{{ count + 1 }}                  // Addition (if both numeric)
{{ items[loop.index0 - 1] }}     // Subtraction in array index
```

### Filters

Filters transform values using the pipe (`|`) operator:

```hemlock
{{ name|upper }}              // Uppercase
{{ name|lower }}              // Lowercase
{{ name|trim }}               // Trim whitespace
{{ name|capitalize }}         // Capitalize first letter
{{ name|title }}              // Title Case
{{ name|length }}             // Length of string/array
{{ name|default("fallback") }} // Default value if null/empty
{{ name|first }}              // First element/character
{{ name|last }}               // Last element/character
{{ items|join(", ") }}        // Join array with separator
{{ items|reverse }}           // Reverse array/string
{{ items|sort }}              // Sort array
{{ html|escape }}             // HTML escape (also: |e)
{{ text|replace("a", "b") }}  // Replace all occurrences
{{ text|truncate(20) }}       // Truncate with "..."
{{ text|truncate(20, "..") }} // Truncate with custom suffix
{{ value|int }}               // Convert to integer
{{ value|float }}             // Convert to float
{{ value|string }}            // Convert to string
{{ value|abs }}               // Absolute value
{{ value|round }}             // Round to nearest integer
{{ obj|tojson }}              // Convert to JSON string
```

Multiple filters can be chained:

```hemlock
{{ name|trim|lower|capitalize }}
```

### Whitespace Control

Use `-` to strip whitespace before or after tags:

```hemlock
{%- if true -%}     // Strip whitespace before and after
{{- value -}}       // Strip whitespace around output
{#- comment -#}     // Strip whitespace around comment

// Example:
{{ "a" }}  {%- if true -%}  b  {%- endif -%}  {{ "c" }}
// Output: "abc" (whitespace stripped)
```

### Conditionals

```hemlock
{% if condition %}
    Content if true
{% endif %}

{% if condition %}
    Content if true
{% else %}
    Content if false
{% endif %}

{% if score >= 90 %}
    A
{% elif score >= 80 %}
    B
{% elif score >= 70 %}
    C
{% else %}
    F
{% endif %}
```

#### Condition Operators

- Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Boolean: `and`, `or`, `not`
- Membership: `in` (for arrays, strings, and objects)
- Type tests: `is` (string, number, boolean, etc.)

```hemlock
{% if user.active and user.verified %}
{% if role == "admin" or role == "moderator" %}
{% if not logged_in %}
{% if "admin" in user.roles %}          // Check array membership
{% if "name" in user %}                  // Check object has property
{% if message.content is string %}       // Type test
```

#### Type Tests

The `is` operator tests the type of a value:

```hemlock
{% if value is string %}      // True if string
{% if value is number %}      // True if any numeric type
{% if value is integer %}     // True if integer type
{% if value is float %}       // True if float type
{% if value is boolean %}     // True if bool
{% if value is sequence %}    // True if array or string
{% if value is mapping %}     // True if object
{% if value is null %}        // True if null
{% if value is defined %}     // True if not null
```

### Loops

```hemlock
{% for item in items %}
    {{ item }}
{% endfor %}

{% for user in users %}
    {{ user.name }} ({{ user.email }})
{% endfor %}
```

#### Loop Variables

Inside a loop, a special `loop` variable is available:

| Variable | Description |
|----------|-------------|
| `loop.index` | Current iteration (1-indexed) |
| `loop.index0` | Current iteration (0-indexed) |
| `loop.first` | `true` if first iteration |
| `loop.last` | `true` if last iteration |
| `loop.length` | Total number of items |

```hemlock
{% for item in items %}
    {{ loop.index }}. {{ item }}{% if not loop.last %}, {% endif %}
{% endfor %}
```

### Set Statement

Set variables within the template:

```hemlock
{% set name = "Alice" %}
{% set count = 42 %}
{% set greeting = "Hello, " + name %}
{{ greeting }}  // "Hello, Alice"
```

### Namespace

Use `namespace()` for mutable state that persists across loop iterations:

```hemlock
{% set counter = namespace(value=0) %}
{% for item in items %}
    {% set counter.value = counter.value + 1 %}
    {{ counter.value }}. {{ item }}
{% endfor %}
```

### Comments

Comments are enclosed in `{# ... #}` and are completely removed from output:

```hemlock
{# This is a comment and won't appear in output #}
{{ name }} {# inline comment #}
```

## API Reference

### render(template, context): string

Renders a Jinja-style template with the given context.

**Parameters:**
- `template: string` - Template string with Jinja syntax
- `context: object` - Object containing variables for substitution

**Returns:** Rendered template string

**Throws:** On syntax errors (unclosed tags) or invalid syntax

```hemlock
import { render } from "@stdlib/jinja";

let result = render("Hello, {{ name }}!", { name: "World" });
print(result);  // "Hello, World!"

// With nested objects
let user = { name: "Alice", address: { city: "Boston" } };
let result2 = render("{{ user.name }} lives in {{ user.address.city }}", { user: user });
print(result2);  // "Alice lives in Boston"
```

### escape(str): string

Escapes HTML special characters to prevent XSS attacks.

**Parameters:**
- `str: string` - String to escape

**Returns:** Escaped string safe for HTML output

**Escaped characters:**
- `&` becomes `&amp;`
- `<` becomes `&lt;`
- `>` becomes `&gt;`
- `"` becomes `&quot;`
- `'` becomes `&#39;`

```hemlock
import { escape } from "@stdlib/jinja";

let unsafe = "<script>alert('xss')</script>";
print(escape(unsafe));  // "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;"
```

### namespace(initial?): object

Creates a namespace object for mutable state in templates.

**Parameters:**
- `initial: object` - Optional initial values

**Returns:** Namespace object

```hemlock
import { namespace } from "@stdlib/jinja";

let ns = namespace({ count: 0, total: 100 });
```

## Examples

### LLM Chat Template (Qwen3 Style)

```hemlock
import { render, namespace } from "@stdlib/jinja";

let template = `
{%- for message in messages -%}
{%- if message.role == 'system' -%}
<|im_start|>system
{{ message.content }}<|im_end|>
{%- elif message.role == 'user' -%}
<|im_start|>user
{{ message.content }}<|im_end|>
{%- elif message.role == 'assistant' -%}
<|im_start|>assistant
{{ message.content }}<|im_end|>
{%- endif -%}
{%- endfor -%}
<|im_start|>assistant
`;

let messages = [
    { role: "system", content: "You are a helpful assistant." },
    { role: "user", content: "Hello!" }
];

print(render(template, { messages: messages }));
```

### Tool Calls with JSON

```hemlock
import { render } from "@stdlib/jinja";

let template = `
{%- if tools -%}
Available tools:
{%- for tool in tools %}
{{ tool|tojson }}
{%- endfor %}
{%- endif -%}
`;

let context = {
    tools: [
        { name: "search", description: "Search the web" },
        { name: "calculate", description: "Perform math" }
    ]
};

print(render(template, context));
```

### HTML Template

```hemlock
import { render } from "@stdlib/jinja";

let template = `
<html>
<head><title>{{ title }}</title></head>
<body>
    <h1>{{ heading|escape }}</h1>
    {% if items %}
    <ul>
        {% for item in items %}
        <li>{{ loop.index }}. {{ item|escape }}</li>
        {% endfor %}
    </ul>
    {% else %}
    <p>No items found.</p>
    {% endif %}
</body>
</html>
`;

let context = {
    title: "My Page",
    heading: "Welcome!",
    items: ["Apple", "Banana", "Cherry"]
};

print(render(template, context));
```

### Dynamic Content with Type Checks

```hemlock
import { render } from "@stdlib/jinja";

let template = `
{%- for content in message.content -%}
{%- if content is string -%}
{{ content }}
{%- elif 'text' in content -%}
{{ content.text }}
{%- elif 'image' in content -%}
[Image: {{ content.image }}]
{%- endif -%}
{%- endfor -%}
`;

let context = {
    message: {
        content: [
            "Hello, ",
            { text: "world" },
            { image: "photo.jpg" }
        ]
    }
};

print(render(template, context));
```

## Compatibility Notes

This implementation supports most common Jinja2 features needed for LLM chat templates:

**Supported:**
- Variable interpolation with dot notation and array indexing
- Expression evaluation (+, - operators)
- Built-in filters (see Filters section)
- `if`/`elif`/`else`/`endif` conditionals
- `for`/`endfor` loops with `loop` variable
- `set` variable assignment
- `namespace()` for mutable state
- Type tests with `is`
- Property checks with `in`
- Whitespace control (`{%-`, `-%}`, `{{-`, `-}}`)
- Comments `{# ... #}`
- String literals in expressions

**Not Supported:**
- Macros and blocks
- Template inheritance (`extends`, `include`)
- Raw blocks
- Custom filters
- Line statements (`#`)
- Complex expressions beyond + and -

For complex logic, process your data in Hemlock before passing it to the template.
