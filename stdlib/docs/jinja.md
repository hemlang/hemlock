# @stdlib/jinja - Jinja Template Rendering

## Overview

The jinja module provides Jinja2-compatible template rendering for Hemlock. It supports variable interpolation, control flow structures, filters, and comments using familiar Jinja syntax.

## Quick Start

```hemlock
import { render } from "@stdlib/jinja";

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
```

Multiple filters can be chained:

```hemlock
{{ name|trim|lower|capitalize }}
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
- Membership: `in` (for arrays and strings)

```hemlock
{% if user.active and user.verified %}
{% if role == "admin" or role == "moderator" %}
{% if not logged_in %}
{% if "admin" in user.roles %}
{% if "foo" in some_string %}
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

## Examples

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

### Email Template

```hemlock
import { render } from "@stdlib/jinja";

let email_template = `
Dear {{ recipient.name|default("Customer") }},

Thank you for your order #{{ order.id }}.

Items ordered:
{% for item in order.items %}
  - {{ item.name }} x {{ item.quantity }} @ ${{ item.price }}
{% endfor %}

Total: ${{ order.total }}

{% if order.notes %}
Notes: {{ order.notes }}
{% endif %}

Best regards,
{{ sender.name }}
`;

let context = {
    recipient: { name: "John" },
    order: {
        id: 12345,
        items: [
            { name: "Widget", quantity: 2, price: 9.99 },
            { name: "Gadget", quantity: 1, price: 24.99 }
        ],
        total: 44.97,
        notes: null
    },
    sender: { name: "Support Team" }
};

print(render(email_template, context));
```

### Conditional Content

```hemlock
import { render } from "@stdlib/jinja";

let template = `
User: {{ user.name }}
Status: {% if user.active %}Active{% else %}Inactive{% endif %}
Role: {% if user.role == "admin" %}Administrator
      {% elif user.role == "mod" %}Moderator
      {% else %}Regular User{% endif %}
`;

let context = {
    user: { name: "Alice", active: true, role: "admin" }
};

print(render(template, context));
```

### Data Transformation

```hemlock
import { render } from "@stdlib/jinja";

let template = `
Names: {{ names|sort|join(", ") }}
First: {{ names|first }}
Last: {{ names|sort|last }}
Count: {{ names|length }}
Reversed: {{ names|reverse|join(" -> ") }}
`;

let context = {
    names: ["Charlie", "Alice", "Bob"]
};

print(render(template, context));
// Output:
// Names: Alice, Bob, Charlie
// First: Charlie
// Last: Charlie
// Count: 3
// Reversed: Bob -> Alice -> Charlie
```

## Differences from Jinja2

This implementation provides a subset of Jinja2 functionality suitable for Hemlock:

**Supported:**
- Variable interpolation with dot notation and array indexing
- Built-in filters (see Filters section)
- `if`/`elif`/`else`/`endif` conditionals
- `for`/`endfor` loops with `loop` variable
- Comments `{# ... #}`
- String literals in expressions
- Comparison and boolean operators

**Not Supported:**
- Macros and blocks
- Template inheritance (`extends`, `include`)
- Set statements
- Raw blocks
- Custom filters (use Hemlock functions instead)
- Whitespace control (`{%-`, `-%}`)
- Line statements (`#`)
- Complex expressions (function calls, arithmetic in templates)

For complex logic, process your data in Hemlock before passing it to the template.

## Error Handling

The `render()` function throws errors for:
- Unclosed variable tags `{{ ... `
- Unclosed block tags `{% ... `
- Unclosed comment tags `{# ... `
- Missing `{% endif %}` for `{% if %}`
- Missing `{% endfor %}` for `{% for %}`
- Invalid `{% for %}` syntax

```hemlock
import { render } from "@stdlib/jinja";

try {
    render("{{ name", {});  // Missing closing }}
} catch (e) {
    print("Error: " + e);  // "Error: Unclosed variable tag starting at position 0"
}
```
