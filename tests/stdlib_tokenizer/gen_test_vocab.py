#!/usr/bin/env python3
"""Generate a small test BPE vocabulary file for testing the tokenizer module.

Creates a vocab with:
- 256 single-byte tokens (ranks 0-255)
- Common English bigrams/trigrams as merges (ranks 256+)
"""
import base64

vocab = []

# Single byte tokens (ranks 0-255)
for i in range(256):
    b64 = base64.b64encode(bytes([i])).decode()
    vocab.append((b64, i))

# Common merges (each pair is the concatenation of byte values)
merges = [
    (b" t", 256),    # space + t
    (b"he", 257),    # h + e
    (b"in", 258),    # i + n
    (b"re", 259),    # r + e
    (b"on", 260),    # o + n
    (b"th", 261),    # t + h
    (b"er", 262),    # e + r
    (b"an", 263),    # a + n
    (b" the", 264),  # (space+t) + h + e => multi-byte merge
    (b"the", 265),   # t + h + e
    (b"ll", 266),    # l + l
    (b"He", 267),    # H + e
    (b"lo", 268),    # l + o
    (b"Hel", 269),   # He + l
    (b"Hell", 270),  # Hel + l
    (b"Hello", 271), # Hell + o
    (b", ", 272),    # comma + space
    (b"wo", 273),    # w + o
    (b"or", 274),    # o + r
    (b"wor", 275),   # wo + r
    (b"ld", 276),    # l + d
    (b"worl", 277),  # wor + l  (note: not standard BPE order but works for testing)
    (b"world", 278), # worl + d
    (b"!", 33),      # already exists as single byte, skip
]

for token_bytes, rank in merges:
    if rank <= 255:
        continue  # Skip single-byte duplicates
    b64 = base64.b64encode(token_bytes).decode()
    vocab.append((b64, rank))

with open("test_vocab.bpe", "w") as f:
    for b64, rank in vocab:
        f.write(f"{b64} {rank}\n")

print(f"Generated {len(vocab)} vocab entries")
