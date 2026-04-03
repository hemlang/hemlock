# @stdlib/prompt - LLM Prompt Templating

## Overview

The prompt module provides utilities for building multi-turn LLM conversations, applying chat templates, and managing message arrays. It uses `@stdlib/jinja` under the hood for template rendering.

## Quick Start

```hemlock
import { system, user, format_chat } from "@stdlib/prompt";

let msgs = [
    system("You are a helpful assistant."),
    user("What is 2 + 2?")
];

let prompt = format_chat(msgs, "chatml");
print(prompt);
// <|im_start|>system
// You are a helpful assistant.<|im_end|>
// <|im_start|>user
// What is 2 + 2?<|im_end|>
// <|im_start|>assistant
```

## API Reference

### Message Constructors

#### `message(role: string, content: string): object`

Create a message with any role.

```hemlock
let msg = message("system", "You are helpful.");
print(msg.role);     // "system"
print(msg.content);  // "You are helpful."
```

#### `system(content: string): object`

Create a system message.

```hemlock
let msg = system("You are a coding assistant.");
```

#### `user(content: string): object`

Create a user message.

```hemlock
let msg = user("Write a function to add two numbers.");
```

#### `assistant(content: string): object`

Create an assistant message.

```hemlock
let msg = assistant("Here's a function that adds two numbers...");
```

#### `tool(content: string, name?: string): object`

Create a tool/function result message with optional tool name.

```hemlock
let msg = tool("42", "calculator");
print(msg.role);    // "tool"
print(msg.name);    // "calculator"
```

### Template Formatting

#### `format_chat(messages: array, template_name: string): string`

Format a message array with a named built-in template.

**Parameters:**
- `messages` - Array of message objects (each with `.role` and `.content`)
- `template_name` - One of: `chatml`, `llama3`, `alpaca`, `vicuna`, `zephyr`, `gemma`, `plain`

**Returns:** Formatted prompt string

```hemlock
import { system, user, assistant, format_chat } from "@stdlib/prompt";

let msgs = [
    system("You are helpful."),
    user("Hi!"),
    assistant("Hello!"),
    user("How are you?")
];

// ChatML format
print(format_chat(msgs, "chatml"));

// Llama 3 format
print(format_chat(msgs, "llama3"));

// Plain text format
print(format_chat(msgs, "plain"));
```

#### `apply_template(template: string, messages: array, context?: object): string`

Apply a custom Jinja template to a message array. The `messages` variable is automatically available in the template. Additional variables can be passed via `context`.

```hemlock
import { system, user, apply_template } from "@stdlib/prompt";

let tmpl = "{% for msg in messages %}[{{ msg.role|upper }}] {{ msg.content }}\n{% endfor %}";
let msgs = [system("Be brief."), user("Hello!")];

print(apply_template(tmpl, msgs));
// [SYSTEM] Be brief.
// [USER] Hello!

// With extra context
let tmpl2 = "Model: {{ model }}\n{% for msg in messages %}{{ msg.content }}\n{% endfor %}";
print(apply_template(tmpl2, msgs, { model: "gpt-4" }));
```

### Conversation Builder

#### `conversation(template_name?: string): object`

Create a conversation builder for incremental message construction. Defaults to `"chatml"` template.

**Methods:**
- `add_system(content)` - Add a system message (returns self for chaining)
- `add_user(content)` - Add a user message (returns self for chaining)
- `add_assistant(content)` - Add an assistant message (returns self for chaining)
- `add_message(role, content)` - Add a message with any role (returns self for chaining)
- `format()` - Format all messages using the configured template
- `format_with(template)` - Format using a custom Jinja template
- `count()` - Get the number of messages
- `last_message()` - Get the last message object (or null)
- `reset()` - Remove all messages (returns self for chaining)
- `by_role(role)` - Get messages filtered by role

```hemlock
import { conversation } from "@stdlib/prompt";

let conv = conversation("chatml");
conv.add_system("You are a math tutor.");
conv.add_user("What is calculus?");
conv.add_assistant("Calculus is the study of continuous change...");
conv.add_user("Can you give an example?");

print(conv.count());          // 4
print(conv.last_message().role);  // "user"
print(conv.format());         // Full ChatML-formatted prompt

// Filter by role
let user_msgs = conv.by_role("user");
print(user_msgs.length);      // 2
```

### Utility Functions

#### `templates(): array`

List all available built-in template names.

```hemlock
import { templates } from "@stdlib/prompt";

for (name in templates()) {
    print(name);
}
// chatml, llama3, alpaca, vicuna, zephyr, gemma, plain
```

#### `get_template_source(name: string): string`

Get the raw Jinja template string for a built-in template. Useful for inspecting or modifying templates.

```hemlock
import { get_template_source } from "@stdlib/prompt";

let tmpl = get_template_source("chatml");
print(tmpl);
```

## Built-in Templates

### ChatML (`chatml`)

Used by OpenAI, Qwen, Yi, and many other models.

```
<|im_start|>system
{content}<|im_end|>
<|im_start|>user
{content}<|im_end|>
<|im_start|>assistant
```

### Llama 3 (`llama3`)

Used by Meta's Llama 3 and Llama 3.1 models.

```
<|start_header_id|>system<|end_header_id|>

{content}<|eot_id|><|start_header_id|>user<|end_header_id|>

{content}<|eot_id|><|start_header_id|>assistant<|end_header_id|>

```

### Alpaca (`alpaca`)

Instruction-following format used by Stanford Alpaca.

```
### Instruction:
{system content}

### Input:
{user content}

### Response:
```

### Vicuna (`vicuna`)

Used by Vicuna/LMSys models.

```
{system content}

USER: {user content}
ASSISTANT:
```

### Zephyr (`zephyr`)

Used by HuggingFace Zephyr (Mistral-based).

```
<|system|>
{content}<|endoftext|>
<|user|>
{content}<|endoftext|>
<|assistant|>
```

### Gemma (`gemma`)

Used by Google Gemma models. System messages are formatted as user turns.

```
<start_of_turn>user
{content}<end_of_turn>
<start_of_turn>model
```

### Plain (`plain`)

Simple text format with no special tokens, useful for debugging.

```
[system]: {content}
[user]: {content}
[assistant]:
```

## Examples

### Multi-turn Conversation

```hemlock
import { conversation } from "@stdlib/prompt";

let conv = conversation("chatml");
conv.add_system("You are a Python expert.");

// Simulate a multi-turn conversation
conv.add_user("How do I read a file in Python?");
conv.add_assistant("Use open() with a context manager:\n```python\nwith open('file.txt') as f:\n    content = f.read()\n```");
conv.add_user("What about binary files?");

print(conv.format());
```

### Custom Template for Tool Use

```hemlock
import { system, user, tool, apply_template } from "@stdlib/prompt";

let tmpl = `{%- for msg in messages -%}
{%- if msg.role == 'system' -%}
<|system|>{{ msg.content }}</s>
{%- elif msg.role == 'user' -%}
<|user|>{{ msg.content }}</s>
{%- elif msg.role == 'tool' -%}
<|tool|>[{{ msg.name }}] {{ msg.content }}</s>
{%- endif -%}
{%- endfor -%}
<|assistant|>`;

let msgs = [
    system("You can use tools."),
    user("What is 6 * 7?"),
    tool("42", "calculator")
];

print(apply_template(tmpl, msgs));
```

### Comparing Template Formats

```hemlock
import { system, user, format_chat, templates } from "@stdlib/prompt";

let msgs = [
    system("You are helpful."),
    user("Hello!")
];

for (name in templates()) {
    print("=== " + name + " ===");
    print(format_chat(msgs, name));
    print("");
}
```
