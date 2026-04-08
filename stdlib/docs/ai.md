# Hemlock AI Module

A standard library module providing a unified interface to AI/LLM APIs for chat completions, embeddings, and streaming.

## Overview

The AI module makes it trivial to call AI models from Hemlock:

- **Multi-provider** — OpenAI, Anthropic, and Ollama (local models) with consistent API
- **One-liner chat** — `chat("question")` auto-detects provider from model name
- **Embeddings** — Generate vectors for semantic search and RAG
- **Streaming** — Token-by-token output with callbacks
- **Auto-detection** — Model names like "gpt-4o" route to OpenAI, "claude-*" to Anthropic, "llama3" to Ollama

**Status:** Production-ready. Built on `@stdlib/http` — no additional dependencies.

## Setup

Set API keys via environment variables:

```bash
export OPENAI_API_KEY="sk-..."
export ANTHROPIC_API_KEY="sk-ant-..."
```

Ollama requires no API key (runs locally on port 11434).

## Quick Start

```hemlock
import { chat, embed, stream_chat } from "@stdlib/ai";

// One-liner chat
let answer = chat("What is Hemlock?");
print(answer);

// With a specific model
let response = chat("Explain pointers", model: "claude-sonnet-4-20250514");

// Stream the response
stream_chat("Tell me a story", fn(chunk) {
    write(chunk);
});
print("");

// Generate embeddings
let vec = embed("Hello world");
print("Embedding dimensions:", vec.length);
```

## API Reference

### Quick Functions

| Function | Description |
|----------|-------------|
| `chat(prompt, model?, provider?, api_key?, system?, temperature?, max_tokens?, timeout_ms?)` | One-shot chat completion, returns string |
| `complete(messages, model?, provider?, api_key?, temperature?, max_tokens?, timeout_ms?)` | Multi-turn chat with message history |
| `embed(text, model?, provider?, api_key?, timeout_ms?)` | Generate embedding vector |
| `embed_batch(texts, model?, provider?, api_key?, timeout_ms?)` | Batch embed multiple texts |
| `stream_chat(prompt, on_chunk, model?, provider?, api_key?, system?, temperature?, max_tokens?, timeout_ms?)` | Stream completion with callback |

### Provider Clients

#### OpenAI

```hemlock
import { openai } from "@stdlib/ai";

let client = openai("sk-...");
// or: let client = openai();  // uses OPENAI_API_KEY env var

let answer = client.chat("Hello!");
let answer = client.chat("Hello!", model: "gpt-4o", temperature: 0.7);

// Multi-turn
let msgs = [
    { role: "system", content: "You are a pirate." },
    { role: "user", content: "Hello!" },
];
let response = client.complete(msgs);

// Embeddings
let vec = client.embed("Hello world");
let vec = client.embed("Hello world", model: "text-embedding-3-large");

// Streaming
client.stream("Tell me a joke", fn(chunk) { write(chunk); });

// Custom base URL (for proxies, Azure, etc.)
let client = openai("sk-...", base_url: "https://my-proxy.com/v1");
```

#### Anthropic

```hemlock
import { anthropic } from "@stdlib/ai";

let client = anthropic("sk-ant-...");
// or: let client = anthropic();  // uses ANTHROPIC_API_KEY env var

let answer = client.chat("Hello!", model: "claude-sonnet-4-20250514");

// With system prompt
let answer = client.chat("Explain memory management",
    system: "You are a Hemlock language expert.",
    max_tokens: 2048
);

// Streaming
client.stream("Write a haiku", fn(chunk) { write(chunk); });
```

#### Ollama (Local Models)

```hemlock
import { ollama } from "@stdlib/ai";

let client = ollama();  // localhost:11434
// or: let client = ollama(base_url: "http://192.168.1.100:11434");

let answer = client.chat("Hello!", model: "llama3");
let vec = client.embed("Hello!", model: "nomic-embed-text");

// Streaming with local model
client.stream("Explain Rust vs C", fn(chunk) { write(chunk); }, model: "mistral");
```

### Provider Auto-Detection

The `chat()` and `complete()` functions auto-detect the provider from the model name:

| Model prefix | Provider |
|-------------|----------|
| `gpt-*`, `o1-*`, `o3-*`, `o4-*`, `text-embedding-*` | OpenAI |
| `claude-*` | Anthropic |
| `llama*`, `mistral*`, `gemma*`, `phi*`, `qwen*`, `deepseek*` | Ollama |
| Other | OpenAI (default) |

### Constants

| Constant | Value |
|----------|-------|
| `PROVIDER_OPENAI` | `"openai"` |
| `PROVIDER_ANTHROPIC` | `"anthropic"` |
| `PROVIDER_OLLAMA` | `"ollama"` |

## Examples

### RAG pipeline with embeddings

```hemlock
import { embed, chat } from "@stdlib/ai";
import { cosine_similarity } from "@stdlib/matrix";

// Index some documents
let docs = [
    "Hemlock uses manual memory management with alloc/free.",
    "Hemlock supports async/await with pthread-based parallelism.",
    "Hemlock has a foreign function interface (FFI) for calling C libraries.",
];

let embeddings = [];
for (doc in docs) {
    embeddings.push(embed(doc));
}

// Query
let query = "How does Hemlock handle concurrency?";
let query_vec = embed(query);

// Find most relevant document
let best_idx = 0;
let best_score = -1.0;
for (let i = 0; i < embeddings.length; i++) {
    let score = cosine_similarity(query_vec, embeddings[i]);
    if (score > best_score) {
        best_score = score;
        best_idx = i;
    }
}

// Generate answer with context
let answer = chat(query,
    system: "Answer based on this context: " + docs[best_idx],
    model: "gpt-4o"
);
print(answer);
```

### Streaming chatbot

```hemlock
import { openai } from "@stdlib/ai";

let client = openai();
let history = [
    { role: "system", content: "You are a helpful Hemlock programming assistant." },
];

loop {
    let input = read_line();
    if (input == null || input == "quit") { break; }

    history.push({ role: "user", content: input });

    write("Assistant: ");
    let response = client.stream(input, fn(chunk) {
        write(chunk);
    });
    print("");

    history.push({ role: "assistant", content: response });
}
```

### Compare models

```hemlock
import { chat } from "@stdlib/ai";

let prompt = "Explain pointer arithmetic in one sentence.";

let gpt = chat(prompt, model: "gpt-4o");
let claude = chat(prompt, model: "claude-sonnet-4-20250514");

print("GPT-4o: " + gpt);
print("Claude: " + claude);
```
