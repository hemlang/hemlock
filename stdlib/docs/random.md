# @stdlib/random - Random Utilities

The `random` module provides common random operations including shuffling, sampling, dice rolling, and random string generation.

## Overview

This module builds on the basic `rand()` function from `@stdlib/math` to provide:
- **Number generation**: `randint`, `randf`
- **Array operations**: `shuffle`, `choice`, `sample`, `choices`
- **Weighted selection**: `weighted_choice`
- **Convenience**: `coin_flip`, `dice`, `roll`, `random_bool`
- **String generation**: `random_string`, `random_hex`, `uuid4`

## Usage

```hemlock
import { shuffle, choice, sample, randint, randf } from "@stdlib/random";
import { coin_flip, dice, roll, random_string, uuid4 } from "@stdlib/random";
import { set_seed } from "@stdlib/random";

// Seed for reproducible results
set_seed(42);

// Random integer from 1 to 100
let n = randint(1, 100);

// Shuffle an array
let cards = [1, 2, 3, 4, 5];
shuffle(cards);

// Pick a random element
let colors = ["red", "green", "blue"];
let color = choice(colors);

// Generate a UUID
let id = uuid4();
```

---

## Number Generation

### randint(min, max): i32

Random integer in range [min, max] (inclusive on both ends).

```hemlock
let n = randint(1, 10);   // 1 to 10 inclusive
let die = randint(1, 6);  // Standard die roll
let coin = randint(0, 1); // 0 or 1
```

### randf(min, max): f64

Random float in range [min, max).

```hemlock
let f = randf(0.0, 1.0);    // 0.0 to 0.999...
let temp = randf(20.0, 30.0); // Temperature between 20 and 30
```

---

## Array Operations

### shuffle(arr): array

Shuffle an array in-place using Fisher-Yates algorithm.

```hemlock
let deck = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
shuffle(deck);
print(deck);  // [7, 2, 9, 1, 5, 10, 3, 8, 4, 6] (random order)
```

**Note:** Modifies the array in-place and returns it.

### choice(arr): any

Return a random element from an array.

```hemlock
let colors = ["red", "green", "blue"];
let color = choice(colors);  // Random color
```

### sample(arr, n): array

Return n random elements without replacement.

```hemlock
let numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
let picks = sample(numbers, 3);  // e.g., [4, 7, 2]
```

**Throws:** Error if n > array length.

### choices(arr, n): array

Return n random elements with replacement (may contain duplicates).

```hemlock
let colors = ["red", "green", "blue"];
let picks = choices(colors, 5);  // e.g., ["red", "blue", "blue", "red", "green"]
```

---

## Weighted Selection

### weighted_choice(items, weights): any

Choose a random element with weighted probability.

```hemlock
let items = ["common", "uncommon", "rare", "legendary"];
let weights = [70, 20, 8, 2];  // 70%, 20%, 8%, 2%

let drop = weighted_choice(items, weights);
// "common" appears ~70% of the time
```

**Parameters:**
- `items`: Array of items to choose from
- `weights`: Array of weights (must be same length, non-negative)

---

## Convenience Functions

### coin_flip(): bool

Flip a coin (50/50 chance).

```hemlock
if (coin_flip()) {
    print("Heads!");
} else {
    print("Tails!");
}
```

### dice(sides?): i32

Roll a die with given number of sides (default: 6).

```hemlock
let d6 = dice();       // Standard 6-sided die
let d20 = dice(20);    // 20-sided die
let d100 = dice(100);  // Percentile die
```

### roll(count, sides?): i32

Roll multiple dice and sum the results.

```hemlock
let damage = roll(2, 6);   // 2d6: sum of two 6-sided dice
let attack = roll(1, 20);  // 1d20
let stats = roll(3, 6);    // 3d6 for character stats
```

### random_bool(probability?): bool

Generate a random boolean with given probability of true.

```hemlock
let hit = random_bool(0.75);  // 75% chance of true
let crit = random_bool(0.05); // 5% chance of critical hit
```

---

## String Generation

### random_string(length, charset?): string

Generate a random string of given length.

```hemlock
// Alphanumeric (default charset)
let token = random_string(16);  // "xK7mP2nQ9rT1vW3z"

// Custom charset
let pin = random_string(4, "0123456789");  // "4729"
let hex_str = random_string(8, "0123456789abcdef");  // "3f8a2c1e"
```

### random_hex(length): string

Generate a random hexadecimal string (lowercase).

```hemlock
let hex = random_hex(16);  // "3f8a2c1e9d4b7a0f"
```

### uuid4(): string

Generate a UUID version 4 (random).

```hemlock
let id = uuid4();  // "550e8400-e29b-41d4-a716-446655440000"
```

**Format:** `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx`
- `4` indicates version 4 (random)
- `y` is one of `8`, `9`, `a`, or `b` (variant bits)

---

## Seeding

### set_seed(s)

Seed the random number generator for reproducible results.

```hemlock
import { set_seed, randint } from "@stdlib/random";

set_seed(12345);
print(randint(1, 100));  // Always the same value for this seed

set_seed(12345);  // Reset to same seed
print(randint(1, 100));  // Same value again
```

---

## Examples

### Card Shuffling

```hemlock
import { shuffle, choice } from "@stdlib/random";

let suits = ["♠", "♥", "♦", "♣"];
let ranks = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"];

let deck: array = [];
for (suit in suits) {
    for (rank in ranks) {
        deck.push(rank + suit);
    }
}

shuffle(deck);
print("Top card: " + deck[0]);
```

### Loot Table

```hemlock
import { weighted_choice } from "@stdlib/random";

fn get_loot(): string {
    let items = ["gold", "potion", "weapon", "armor", "legendary"];
    let weights = [50, 25, 15, 8, 2];
    return weighted_choice(items, weights);
}

print("You found: " + get_loot());
```

### Password Generator

```hemlock
import { random_string, shuffle } from "@stdlib/random";

fn generate_password(length): string {
    let lower = "abcdefghijklmnopqrstuvwxyz";
    let upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    let digits = "0123456789";
    let special = "!@#$%^&*";

    let all_chars = lower + upper + digits + special;
    return random_string(length, all_chars);
}

print(generate_password(16));
```

---

## See Also

- `@stdlib/math` - Basic `rand()`, `rand_range()`, `seed()` functions
- `@stdlib/uuid` - More UUID variants (v1, v7)
- `@stdlib/crypto` - Cryptographically secure random bytes
