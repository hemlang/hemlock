# Hemlock Tokenizer Module

BPE (Byte Pair Encoding) tokenizer for tiktoken-compatible token counting. Use this to stay under LLM context limits or analyze how text maps to tokens.

## Overview

- Load vocabulary files in tiktoken's base64 format
- Encode text to token IDs and decode back
- Count tokens efficiently without building the full array
- Add special tokens (e.g., `<|endoftext|>`)
- Compatible with any BPE vocabulary (GPT-2, GPT-4, etc.)

## Usage

```hemlock
import { Tokenizer } from "@stdlib/tokenizer";
```

## Creating a Tokenizer

### Tokenizer(vocab_path: string): object

Load a BPE vocabulary file and return a tokenizer object.

```hemlock
let tok = Tokenizer("cl100k_base.tiktoken");
```

The vocabulary file must be in tiktoken format: each line contains a base64-encoded token byte sequence, a space, and its rank (token ID):

```
IQ== 0
Ig== 1
Iw== 2
JA== 3
```

The rank serves as both the token ID and the merge priority (lower rank = higher priority merge).

## Tokenizer Methods

### tok.encode(text: string): array\<i32\>

Encode text into an array of token IDs.

```hemlock
let tokens = tok.encode("Hello, world!");
print(tokens);       // [15496, 11, 995, 0]
print(tokens.length); // 4
```

### tok.decode(tokens: array): string

Decode an array of token IDs back into text.

```hemlock
let text = tok.decode([15496, 11, 995, 0]);
print(text);  // "Hello, world!"
```

### tok.count(text: string): i32

Count tokens without building the full array. More memory-efficient than `encode()` when you only need the count.

```hemlock
let n = tok.count("Hello, world!");
print(n);  // 4

// Check if text fits in context window
if (tok.count(prompt) > 4096) {
    print("Warning: prompt exceeds context limit");
}
```

### tok.vocab_size: i32

The number of tokens in the vocabulary.

```hemlock
print(tok.vocab_size);  // e.g., 100256
```

### tok.add_special(token: string, rank: i32): null

Add a special token that is matched exactly before BPE encoding.

```hemlock
tok.add_special("<|endoftext|>", 100257);
tok.add_special("<|fim_prefix|>", 100258);
```

### tok.free(): null

Free the tokenizer and all its resources. The tokenizer must not be used after calling `free()`.

```hemlock
tok.free();
// tok.encode("text");  // Would throw "Tokenizer: use after free"
```

## Convenience Functions

For one-off tokenization, use these functions that create and destroy a tokenizer internally. For repeated use, create a `Tokenizer` object instead.

### encode(text: string, vocab_path: string): array\<i32\>

```hemlock
import { encode } from "@stdlib/tokenizer";
let tokens = encode("Hello!", "vocab.bpe");
```

### decode(tokens: array, vocab_path: string): string

```hemlock
import { decode } from "@stdlib/tokenizer";
let text = decode([15496, 0], "vocab.bpe");
```

### count_tokens(text: string, vocab_path: string): i32

```hemlock
import { count_tokens } from "@stdlib/tokenizer";
let n = count_tokens("Hello, world!", "vocab.bpe");
```

## BPE Algorithm

The tokenizer implements the standard Byte Pair Encoding algorithm:

1. **Pre-tokenize**: Split text into chunks at word/whitespace/punctuation boundaries
2. **Initialize**: Each byte in a chunk starts as its own token
3. **Merge**: Repeatedly find the byte pair with the lowest rank (highest priority) in the vocabulary and merge them into a single token
4. **Lookup**: Map each final merged byte sequence to its token ID (rank)

This produces a deterministic encoding: the same text always produces the same token sequence.

## Vocabulary File Format

The vocabulary file follows tiktoken's format:

```
<base64_encoded_bytes> <rank>
```

Each line maps a byte sequence (base64-encoded) to a rank. The rank is both the token ID returned by `encode()` and the merge priority (lower rank = merge first).

Lines starting with `#` are treated as comments. Empty lines are skipped.

### Generating a Vocabulary File

You can convert a tiktoken vocabulary using Python:

```python
import tiktoken
import base64

enc = tiktoken.get_encoding("cl100k_base")
with open("cl100k_base.tiktoken", "w") as f:
    for token_bytes, rank in sorted(enc._mergeable_ranks.items(), key=lambda x: x[1]):
        f.write(f"{base64.b64encode(token_bytes).decode()} {rank}\n")
```

## Examples

### Context Window Checking

```hemlock
import { Tokenizer } from "@stdlib/tokenizer";

let tok = Tokenizer("cl100k_base.tiktoken");

fn fits_in_context(text: string, max_tokens: i32): bool {
    return tok.count(text) <= max_tokens;
}

let prompt = "Explain quantum computing in detail...";
if (!fits_in_context(prompt, 4096)) {
    print("Prompt too long, truncating...");
}

tok.free();
```

### Token Analysis

```hemlock
import { Tokenizer } from "@stdlib/tokenizer";

let tok = Tokenizer("vocab.bpe");
let text = "Hello, world!";
let tokens = tok.encode(text);

print("Text:", text);
print("Tokens:", tokens);
print("Token count:", tokens.length);
print("Decoded:", tok.decode(tokens));

tok.free();
```

### Batch Token Counting

```hemlock
import { Tokenizer } from "@stdlib/tokenizer";
import { read_file } from "@stdlib/fs";

let tok = Tokenizer("cl100k_base.tiktoken");

let files = ["doc1.txt", "doc2.txt", "doc3.txt"];
let total = 0;

for (file in files) {
    let content = read_file(file);
    let count = tok.count(content);
    print(file + ": " + count + " tokens");
    total = total + count;
}

print("Total: " + total + " tokens");
tok.free();
```

## Memory Management

The tokenizer allocates memory for the vocabulary hash table and token data. Always call `tok.free()` when done to release these resources. Using `defer` is recommended:

```hemlock
let tok = Tokenizer("vocab.bpe");
defer tok.free();

// Use tok...
let n = tok.count("some text");
```

The vocabulary is typically the largest allocation (e.g., ~100K entries for GPT-4's cl100k_base).
