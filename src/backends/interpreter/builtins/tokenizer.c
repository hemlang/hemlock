/*
 * Hemlock Tokenizer Builtins
 *
 * BPE (Byte Pair Encoding) tokenizer for tiktoken-compatible token counting.
 * Supports loading vocabulary files in tiktoken's base64 format.
 *
 * Vocabulary file format (one token per line):
 *   <base64_encoded_bytes> <rank>
 *
 * The rank is the token ID and also determines merge priority
 * (lower rank = higher priority merge).
 */

#include "internal.h"
#include "../../../../include/hemlock_limits.h"

#include <ctype.h>

/* ========== BASE64 DECODER ========== */

static const unsigned char b64_table[256] = {
    ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,
    ['E'] = 4,  ['F'] = 5,  ['G'] = 6,  ['H'] = 7,
    ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11,
    ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15,
    ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19,
    ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
    ['Y'] = 24, ['Z'] = 25,
    ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29,
    ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33,
    ['i'] = 34, ['j'] = 35, ['k'] = 36, ['l'] = 37,
    ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
    ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45,
    ['u'] = 46, ['v'] = 47, ['w'] = 48, ['x'] = 49,
    ['y'] = 50, ['z'] = 51,
    ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55,
    ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
    ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63,
};

/* Decode base64 string into bytes. Returns number of decoded bytes. */
static int b64_decode(const char *src, int src_len, uint8_t *dst, int dst_cap) {
    int out = 0;
    int accum = 0;
    int bits = 0;
    for (int i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '=') break;
        if (c == '\n' || c == '\r' || c == ' ') continue;
        accum = (accum << 6) | b64_table[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out < dst_cap) {
                dst[out++] = (uint8_t)((accum >> bits) & 0xFF);
            }
        }
    }
    return out;
}

/* ========== BPE DATA STRUCTURES ========== */

/* A single entry in the vocabulary: byte sequence -> rank (token ID) */
typedef struct {
    uint8_t *bytes;      /* Token byte sequence (owned) */
    int byte_len;        /* Length of byte sequence */
    int32_t rank;        /* Token rank/ID */
} BpeVocabEntry;

/* Hash table entry for byte-sequence -> rank lookup */
typedef struct {
    uint8_t *key;        /* Byte sequence (not owned, points into vocab) */
    int key_len;         /* Key length */
    int32_t value;       /* Rank/token ID, or -1 if empty */
} BpeHashEntry;

/* The BPE tokenizer state */
typedef struct {
    /* Vocabulary: array indexed by rank -> byte sequence */
    BpeVocabEntry *vocab;      /* Array of vocab entries */
    int32_t vocab_size;        /* Number of entries */
    int32_t vocab_capacity;    /* Allocated capacity */

    /* Hash table: byte sequence -> rank (for encoding) */
    BpeHashEntry *hash_table;
    int32_t hash_capacity;

    /* Special token support */
    char **special_tokens;     /* Array of special token strings */
    int32_t *special_ranks;    /* Corresponding ranks */
    int32_t num_special;
    int32_t special_capacity;
} BpeTokenizer;

/* ========== HASH TABLE OPERATIONS ========== */

/* FNV-1a hash for byte sequences */
static uint32_t fnv1a_bytes(const uint8_t *data, int len) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Initialize hash table */
static void bpe_hash_init(BpeTokenizer *tok, int32_t capacity) {
    tok->hash_capacity = capacity;
    tok->hash_table = calloc((size_t)capacity, sizeof(BpeHashEntry));
    if (!tok->hash_table) return;
    for (int32_t i = 0; i < capacity; i++) {
        tok->hash_table[i].value = -1;
    }
}

/* Insert into hash table */
static void bpe_hash_insert(BpeTokenizer *tok, uint8_t *key, int key_len, int32_t rank) {
    if (!tok->hash_table) return;
    uint32_t idx = fnv1a_bytes(key, key_len) % (uint32_t)tok->hash_capacity;
    for (int32_t probe = 0; probe < tok->hash_capacity; probe++) {
        int32_t i = (int32_t)((idx + (uint32_t)probe) % (uint32_t)tok->hash_capacity);
        if (tok->hash_table[i].value == -1) {
            tok->hash_table[i].key = key;
            tok->hash_table[i].key_len = key_len;
            tok->hash_table[i].value = rank;
            return;
        }
    }
}

/* Lookup in hash table. Returns rank or -1 if not found. */
static int32_t bpe_hash_lookup(BpeTokenizer *tok, const uint8_t *key, int key_len) {
    if (!tok->hash_table) return -1;
    uint32_t idx = fnv1a_bytes(key, key_len) % (uint32_t)tok->hash_capacity;
    for (int32_t probe = 0; probe < tok->hash_capacity; probe++) {
        int32_t i = (int32_t)((idx + (uint32_t)probe) % (uint32_t)tok->hash_capacity);
        if (tok->hash_table[i].value == -1) return -1;
        if (tok->hash_table[i].key_len == key_len &&
            memcmp(tok->hash_table[i].key, key, (size_t)key_len) == 0) {
            return tok->hash_table[i].value;
        }
    }
    return -1;
}

/* ========== BPE TOKENIZER LIFECYCLE ========== */

/* Free all tokenizer resources */
static void bpe_tokenizer_free(BpeTokenizer *tok) {
    if (!tok) return;
    if (tok->vocab) {
        for (int32_t i = 0; i < tok->vocab_size; i++) {
            free(tok->vocab[i].bytes);
        }
        free(tok->vocab);
    }
    free(tok->hash_table);
    if (tok->special_tokens) {
        for (int32_t i = 0; i < tok->num_special; i++) {
            free(tok->special_tokens[i]);
        }
        free(tok->special_tokens);
    }
    free(tok->special_ranks);
    free(tok);
}

/* Load vocabulary from tiktoken-format file */
static BpeTokenizer *bpe_tokenizer_load(const char *path, char *err_buf, int err_buf_size) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        snprintf(err_buf, (size_t)err_buf_size, "cannot open vocabulary file: %s", path);
        return NULL;
    }

    BpeTokenizer *tok = calloc(1, sizeof(BpeTokenizer));
    if (!tok) {
        fclose(fp);
        snprintf(err_buf, (size_t)err_buf_size, "out of memory");
        return NULL;
    }

    /* Initial allocation for vocab array */
    tok->vocab_capacity = 4096;
    tok->vocab = calloc((size_t)tok->vocab_capacity, sizeof(BpeVocabEntry));
    if (!tok->vocab) {
        fclose(fp);
        bpe_tokenizer_free(tok);
        snprintf(err_buf, (size_t)err_buf_size, "out of memory");
        return NULL;
    }

    char line[HML_BPE_MAX_VOCAB_LINE_LENGTH];
    int32_t max_rank = -1;

    while (fgets(line, sizeof(line), fp)) {
        /* Skip empty lines and comments */
        if (line[0] == '\n' || line[0] == '#') continue;

        /* Find the space separating base64 and rank */
        char *space = strchr(line, ' ');
        if (!space) continue;

        *space = '\0';
        char *b64_str = line;
        int b64_len = (int)(space - line);
        int32_t rank = (int32_t)strtol(space + 1, NULL, 10);

        /* Decode base64 to bytes */
        uint8_t decoded[HML_BPE_BASE64_DECODE_BUFSIZE];
        int decoded_len = b64_decode(b64_str, b64_len, decoded, HML_BPE_BASE64_DECODE_BUFSIZE);
        if (decoded_len <= 0) continue;

        /* Grow vocab array if needed */
        if (tok->vocab_size >= tok->vocab_capacity) {
            int32_t new_cap = tok->vocab_capacity * HML_GROWTH_FACTOR;
            BpeVocabEntry *new_vocab = realloc(tok->vocab, (size_t)new_cap * sizeof(BpeVocabEntry));
            if (!new_vocab) continue;
            tok->vocab = new_vocab;
            tok->vocab_capacity = new_cap;
        }

        /* Store vocabulary entry */
        uint8_t *bytes_copy = malloc((size_t)decoded_len);
        if (!bytes_copy) continue;
        memcpy(bytes_copy, decoded, (size_t)decoded_len);

        BpeVocabEntry *entry = &tok->vocab[tok->vocab_size];
        entry->bytes = bytes_copy;
        entry->byte_len = decoded_len;
        entry->rank = rank;
        tok->vocab_size++;

        if (rank > max_rank) max_rank = rank;
    }
    fclose(fp);

    if (tok->vocab_size == 0) {
        bpe_tokenizer_free(tok);
        snprintf(err_buf, (size_t)err_buf_size, "vocabulary file is empty or invalid: %s", path);
        return NULL;
    }

    /* Build hash table for encoding (byte sequence -> rank) */
    int32_t hash_cap = tok->vocab_size * 3;  /* ~33% load factor */
    if (hash_cap < HML_BPE_INITIAL_HASH_CAPACITY) {
        hash_cap = HML_BPE_INITIAL_HASH_CAPACITY;
    }
    bpe_hash_init(tok, hash_cap);

    for (int32_t i = 0; i < tok->vocab_size; i++) {
        bpe_hash_insert(tok, tok->vocab[i].bytes, tok->vocab[i].byte_len, tok->vocab[i].rank);
    }

    return tok;
}

/* ========== BPE ENCODING ALGORITHM ========== */

/*
 * BPE encoding for a single chunk of text (pre-tokenized piece).
 * Implements the standard BPE merge algorithm:
 * 1. Start with individual bytes as tokens
 * 2. Repeatedly find the pair with the lowest rank
 * 3. Merge that pair
 * 4. Continue until no more merges
 *
 * Returns an array of token IDs (ranks).
 * The caller must free the returned array.
 */

/* Work item: a linked list node for the BPE merge algorithm */
typedef struct BpeNode {
    uint8_t *bytes;          /* Pointer into combined buffer */
    int byte_len;            /* Length of this token's bytes */
    struct BpeNode *next;
    struct BpeNode *prev;
} BpeNode;

static int bpe_encode_chunk(BpeTokenizer *tok, const uint8_t *data, int data_len,
                            int32_t **out_tokens, int *out_count) {
    if (data_len == 0) {
        *out_tokens = NULL;
        *out_count = 0;
        return 0;
    }

    /* Allocate nodes: one per byte initially */
    int node_capacity = data_len + 1;
    BpeNode *nodes = calloc((size_t)node_capacity, sizeof(BpeNode));
    if (!nodes) return -1;

    /* Make a copy of the input data to work with */
    uint8_t *data_copy = malloc((size_t)data_len);
    if (!data_copy) { free(nodes); return -1; }
    memcpy(data_copy, data, (size_t)data_len);

    /* Initialize doubly-linked list: each node is one byte */
    for (int i = 0; i < data_len; i++) {
        nodes[i].bytes = data_copy + i;
        nodes[i].byte_len = 1;
        nodes[i].prev = (i > 0) ? &nodes[i - 1] : NULL;
        nodes[i].next = (i < data_len - 1) ? &nodes[i + 1] : NULL;
    }

    /* Temporary buffer for pair lookup */
    uint8_t pair_buf[HML_BPE_MAX_TOKEN_LENGTH * 2];

    /* Repeatedly merge the highest-priority (lowest-rank) pair */
    int changed = 1;
    while (changed) {
        changed = 0;
        int32_t best_rank = INT32_MAX;
        BpeNode *best_node = NULL;

        /* Find the pair with the lowest rank */
        for (BpeNode *node = &nodes[0]; node && node->next; node = node->next) {
            if (node->byte_len <= 0) continue;
            BpeNode *next = node->next;
            int pair_len = node->byte_len + next->byte_len;
            if (pair_len > HML_BPE_MAX_TOKEN_LENGTH * 2) continue;

            /* Build the merged byte sequence */
            memcpy(pair_buf, node->bytes, (size_t)node->byte_len);
            memcpy(pair_buf + node->byte_len, next->bytes, (size_t)next->byte_len);

            int32_t rank = bpe_hash_lookup(tok, pair_buf, pair_len);
            if (rank >= 0 && rank < best_rank) {
                best_rank = rank;
                best_node = node;
            }
        }

        if (best_node) {
            /* Merge best_node with best_node->next */
            BpeNode *next = best_node->next;
            int new_len = best_node->byte_len + next->byte_len;

            /* Allocate merged byte sequence */
            uint8_t *merged = malloc((size_t)new_len);
            if (!merged) break;
            memcpy(merged, best_node->bytes, (size_t)best_node->byte_len);
            memcpy(merged + best_node->byte_len, next->bytes, (size_t)next->byte_len);

            /* If best_node->bytes was previously allocated by us (not in data_copy), free it */
            if (best_node->bytes < data_copy || best_node->bytes >= data_copy + data_len) {
                free(best_node->bytes);
            }

            best_node->bytes = merged;
            best_node->byte_len = new_len;

            /* Remove next node from linked list */
            best_node->next = next->next;
            if (next->next) {
                next->next->prev = best_node;
            }

            /* Free next node's separately allocated bytes if any */
            if (next->bytes < data_copy || next->bytes >= data_copy + data_len) {
                free(next->bytes);
            }
            next->bytes = NULL;
            next->byte_len = 0;

            changed = 1;
        }
    }

    /* Collect results: look up each remaining node in the vocab */
    int count = 0;
    for (BpeNode *node = &nodes[0]; node; node = node->next) {
        if (node->byte_len > 0) count++;
    }

    int32_t *tokens = malloc((size_t)count * sizeof(int32_t));
    if (!tokens) {
        /* Clean up allocated bytes */
        for (BpeNode *node = &nodes[0]; node; node = node->next) {
            if (node->bytes && (node->bytes < data_copy || node->bytes >= data_copy + data_len)) {
                free(node->bytes);
            }
        }
        free(data_copy);
        free(nodes);
        return -1;
    }

    int idx = 0;
    for (BpeNode *node = &nodes[0]; node; node = node->next) {
        if (node->byte_len <= 0) continue;
        int32_t rank = bpe_hash_lookup(tok, node->bytes, node->byte_len);
        if (rank >= 0) {
            tokens[idx++] = rank;
        } else {
            /* Unknown token: encode individual bytes as fallback */
            for (int b = 0; b < node->byte_len; b++) {
                int32_t byte_rank = bpe_hash_lookup(tok, &node->bytes[b], 1);
                if (byte_rank >= 0) {
                    /* Grow tokens array if needed */
                    if (idx >= count) {
                        count += node->byte_len;
                        int32_t *new_tokens = realloc(tokens, (size_t)count * sizeof(int32_t));
                        if (!new_tokens) { free(tokens); tokens = NULL; break; }
                        tokens = new_tokens;
                    }
                    tokens[idx++] = byte_rank;
                }
            }
            if (!tokens) break;
        }
    }

    /* Clean up dynamically allocated node bytes */
    for (int i = 0; i < node_capacity; i++) {
        if (nodes[i].bytes && (nodes[i].bytes < data_copy || nodes[i].bytes >= data_copy + data_len)) {
            /* Only free if it was separately allocated (not pointing into data_copy) */
            /* Check if another node already freed this pointer */
            int already_freed = 0;
            for (int j = 0; j < i; j++) {
                if (nodes[j].bytes == nodes[i].bytes) { already_freed = 1; break; }
            }
            if (!already_freed) free(nodes[i].bytes);
        }
    }
    free(data_copy);
    free(nodes);

    *out_tokens = tokens;
    *out_count = idx;
    return 0;
}

/* ========== SIMPLE PRE-TOKENIZER ========== */

/*
 * Split text into chunks for BPE processing.
 * This is a simplified pre-tokenizer that splits on:
 * - Whitespace boundaries (keeping leading whitespace with the following word)
 * - Punctuation boundaries
 * Similar to GPT-style pre-tokenization but without requiring PCRE.
 */

typedef struct {
    const uint8_t *start;
    int len;
} TextChunk;

static int is_letter_or_digit(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c >= 128;  /* Include non-ASCII as letters */
}

static int pretokenize(const uint8_t *text, int text_len,
                       TextChunk **out_chunks, int *out_count) {
    if (text_len == 0) {
        *out_chunks = NULL;
        *out_count = 0;
        return 0;
    }

    int capacity = HML_BPE_INITIAL_WORK_CAPACITY;
    TextChunk *chunks = malloc((size_t)capacity * sizeof(TextChunk));
    if (!chunks) return -1;
    int count = 0;

    int i = 0;
    while (i < text_len) {
        int start = i;

        /* Consume optional leading whitespace (attach to following token) */
        while (i < text_len && (text[i] == ' ' || text[i] == '\t')) i++;

        if (i < text_len) {
            if (text[i] == '\n' || text[i] == '\r') {
                /* Newlines are their own chunk */
                i++;
                if (i < text_len && text[i - 1] == '\r' && text[i] == '\n') i++;
            } else if (is_letter_or_digit(text[i])) {
                /* Consume word characters (letters + digits) */
                while (i < text_len && is_letter_or_digit(text[i])) i++;
            } else {
                /* Single punctuation/symbol character */
                i++;
            }
        }

        if (i > start) {
            if (count >= capacity) {
                capacity *= HML_GROWTH_FACTOR;
                TextChunk *new_chunks = realloc(chunks, (size_t)capacity * sizeof(TextChunk));
                if (!new_chunks) { free(chunks); return -1; }
                chunks = new_chunks;
            }
            chunks[count].start = text + start;
            chunks[count].len = i - start;
            count++;
        }
    }

    *out_chunks = chunks;
    *out_count = count;
    return 0;
}

/* ========== FULL BPE ENCODE ========== */

static int bpe_encode(BpeTokenizer *tok, const char *text, int text_len,
                      int32_t **out_tokens, int *out_count) {
    /* Pre-tokenize the input */
    TextChunk *chunks = NULL;
    int num_chunks = 0;
    if (pretokenize((const uint8_t *)text, text_len, &chunks, &num_chunks) != 0) {
        return -1;
    }

    /* Encode each chunk and collect all tokens */
    int total_capacity = text_len + 16;
    int32_t *all_tokens = malloc((size_t)total_capacity * sizeof(int32_t));
    if (!all_tokens) { free(chunks); return -1; }
    int total_count = 0;

    for (int c = 0; c < num_chunks; c++) {
        int32_t *chunk_tokens = NULL;
        int chunk_count = 0;

        if (bpe_encode_chunk(tok, chunks[c].start, chunks[c].len,
                             &chunk_tokens, &chunk_count) != 0) {
            free(all_tokens);
            free(chunks);
            return -1;
        }

        /* Grow if needed */
        while (total_count + chunk_count > total_capacity) {
            total_capacity *= HML_GROWTH_FACTOR;
            int32_t *new_all = realloc(all_tokens, (size_t)total_capacity * sizeof(int32_t));
            if (!new_all) { free(chunk_tokens); free(all_tokens); free(chunks); return -1; }
            all_tokens = new_all;
        }

        if (chunk_tokens) {
            memcpy(all_tokens + total_count, chunk_tokens, (size_t)chunk_count * sizeof(int32_t));
            total_count += chunk_count;
            free(chunk_tokens);
        }
    }

    free(chunks);
    *out_tokens = all_tokens;
    *out_count = total_count;
    return 0;
}

/* ========== BPE DECODE ========== */

static char *bpe_decode(BpeTokenizer *tok, const int32_t *tokens, int num_tokens, int *out_len) {
    /* Calculate total length */
    int total_len = 0;
    for (int i = 0; i < num_tokens; i++) {
        /* Find the vocab entry for this rank */
        for (int32_t v = 0; v < tok->vocab_size; v++) {
            if (tok->vocab[v].rank == tokens[i]) {
                total_len += tok->vocab[v].byte_len;
                break;
            }
        }
    }

    char *result = malloc((size_t)(total_len + 1));
    if (!result) return NULL;

    int offset = 0;
    for (int i = 0; i < num_tokens; i++) {
        for (int32_t v = 0; v < tok->vocab_size; v++) {
            if (tok->vocab[v].rank == tokens[i]) {
                memcpy(result + offset, tok->vocab[v].bytes, (size_t)tok->vocab[v].byte_len);
                offset += tok->vocab[v].byte_len;
                break;
            }
        }
    }
    result[offset] = '\0';
    *out_len = offset;
    return result;
}

/* ========== HEMLOCK BUILTIN FUNCTIONS ========== */

/*
 * __tokenizer_create(path: string) -> ptr
 *
 * Load a BPE vocabulary from a tiktoken-format file and return
 * an opaque tokenizer handle (ptr).
 */
Value builtin_tokenizer_create(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "tokenizer_create() expects 1 argument (vocab_path), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "tokenizer_create() argument must be a string path, got %s",
                      value_type_name(args[0].type));
        return val_null();
    }

    const char *path = args[0].as.as_string->data;
    char err_buf[HML_ERROR_MESSAGE_SIZE];
    BpeTokenizer *tok = bpe_tokenizer_load(path, err_buf, sizeof(err_buf));
    if (!tok) {
        runtime_error(ctx, "%s", err_buf);
        return val_null();
    }

    return val_ptr(tok);
}

/*
 * __tokenizer_free(tok: ptr) -> null
 *
 * Free the tokenizer and all its resources.
 */
Value builtin_tokenizer_free(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "tokenizer_free() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "tokenizer_free() argument must be a ptr, got %s",
                      value_type_name(args[0].type));
        return val_null();
    }

    BpeTokenizer *tok = (BpeTokenizer *)args[0].as.as_ptr;
    if (tok) {
        bpe_tokenizer_free(tok);
    }
    return val_null();
}

/*
 * __tokenizer_encode(tok: ptr, text: string) -> array<i32>
 *
 * Encode text into token IDs using BPE.
 */
Value builtin_tokenizer_encode(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "tokenizer_encode() expects 2 arguments (tokenizer, text), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "tokenizer_encode() first argument must be a tokenizer ptr, got %s",
                      value_type_name(args[0].type));
        return val_null();
    }
    if (args[1].type != VAL_STRING) {
        runtime_error(ctx, "tokenizer_encode() second argument must be a string, got %s",
                      value_type_name(args[1].type));
        return val_null();
    }

    BpeTokenizer *tok = (BpeTokenizer *)args[0].as.as_ptr;
    if (!tok) {
        runtime_error(ctx, "tokenizer_encode() called with null tokenizer");
        return val_null();
    }

    const char *text = args[1].as.as_string->data;
    int text_len = args[1].as.as_string->length;

    int32_t *tokens = NULL;
    int num_tokens = 0;
    if (bpe_encode(tok, text, text_len, &tokens, &num_tokens) != 0) {
        runtime_error(ctx, "tokenizer_encode() failed: encoding error");
        return val_null();
    }

    /* Build Hemlock array from token IDs */
    Array *arr = array_new();
    for (int i = 0; i < num_tokens; i++) {
        Value v = val_i32(tokens[i]);
        array_push(arr, v);
    }
    free(tokens);

    /* Ownership of array transfers to caller via return value */
    return val_array(arr);
}

/*
 * __tokenizer_decode(tok: ptr, tokens: array) -> string
 *
 * Decode an array of token IDs back to text.
 */
Value builtin_tokenizer_decode(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "tokenizer_decode() expects 2 arguments (tokenizer, tokens), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "tokenizer_decode() first argument must be a tokenizer ptr, got %s",
                      value_type_name(args[0].type));
        return val_null();
    }
    if (args[1].type != VAL_ARRAY) {
        runtime_error(ctx, "tokenizer_decode() second argument must be an array, got %s",
                      value_type_name(args[1].type));
        return val_null();
    }

    BpeTokenizer *tok = (BpeTokenizer *)args[0].as.as_ptr;
    if (!tok) {
        runtime_error(ctx, "tokenizer_decode() called with null tokenizer");
        return val_null();
    }

    Array *arr = args[1].as.as_array;
    int count = arr->length;
    int32_t *token_ids = malloc((size_t)count * sizeof(int32_t));
    if (!token_ids && count > 0) {
        runtime_error(ctx, "tokenizer_decode() out of memory");
        return val_null();
    }

    for (int i = 0; i < count; i++) {
        Value elem = array_get(arr, i, ctx);
        token_ids[i] = value_to_int(elem);
    }

    int result_len = 0;
    char *decoded = bpe_decode(tok, token_ids, count, &result_len);
    free(token_ids);

    if (!decoded) {
        runtime_error(ctx, "tokenizer_decode() failed: decode error");
        return val_null();
    }

    Value result = val_string(decoded);
    free(decoded);
    return result;
}

/*
 * __tokenizer_count(tok: ptr, text: string) -> i32
 *
 * Count the number of tokens in text without returning the full array.
 * More efficient for just checking context limits.
 */
Value builtin_tokenizer_count(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "tokenizer_count() expects 2 arguments (tokenizer, text), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "tokenizer_count() first argument must be a tokenizer ptr, got %s",
                      value_type_name(args[0].type));
        return val_null();
    }
    if (args[1].type != VAL_STRING) {
        runtime_error(ctx, "tokenizer_count() second argument must be a string, got %s",
                      value_type_name(args[1].type));
        return val_null();
    }

    BpeTokenizer *tok = (BpeTokenizer *)args[0].as.as_ptr;
    if (!tok) {
        runtime_error(ctx, "tokenizer_count() called with null tokenizer");
        return val_null();
    }

    const char *text = args[1].as.as_string->data;
    int text_len = args[1].as.as_string->length;

    int32_t *tokens = NULL;
    int num_tokens = 0;
    if (bpe_encode(tok, text, text_len, &tokens, &num_tokens) != 0) {
        runtime_error(ctx, "tokenizer_count() failed: encoding error");
        return val_null();
    }
    free(tokens);

    return val_i32(num_tokens);
}

/*
 * __tokenizer_vocab_size(tok: ptr) -> i32
 *
 * Return the number of tokens in the vocabulary.
 */
Value builtin_tokenizer_vocab_size(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "tokenizer_vocab_size() expects 1 argument, got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_PTR) {
        runtime_error(ctx, "tokenizer_vocab_size() first argument must be a tokenizer ptr, got %s",
                      value_type_name(args[0].type));
        return val_null();
    }

    BpeTokenizer *tok = (BpeTokenizer *)args[0].as.as_ptr;
    if (!tok) {
        return val_i32(0);
    }

    return val_i32(tok->vocab_size);
}

/*
 * __tokenizer_add_special(tok: ptr, token: string, rank: i32) -> null
 *
 * Add a special token to the tokenizer vocabulary.
 * Special tokens are matched exactly before BPE encoding.
 */
Value builtin_tokenizer_add_special(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "tokenizer_add_special() expects 3 arguments (tokenizer, token, rank), got %d", num_args);
        return val_null();
    }
    if (args[0].type != VAL_PTR || args[1].type != VAL_STRING) {
        runtime_error(ctx, "tokenizer_add_special() invalid argument types");
        return val_null();
    }

    BpeTokenizer *tok = (BpeTokenizer *)args[0].as.as_ptr;
    if (!tok) {
        runtime_error(ctx, "tokenizer_add_special() called with null tokenizer");
        return val_null();
    }

    const char *token_str = args[1].as.as_string->data;
    int32_t rank = value_to_int(args[2]);

    /* Grow special tokens array if needed */
    if (tok->num_special >= tok->special_capacity) {
        int32_t new_cap = tok->special_capacity == 0 ? 16 : tok->special_capacity * HML_GROWTH_FACTOR;
        char **new_tokens = realloc(tok->special_tokens, (size_t)new_cap * sizeof(char *));
        int32_t *new_ranks = realloc(tok->special_ranks, (size_t)new_cap * sizeof(int32_t));
        if (!new_tokens || !new_ranks) {
            free(new_tokens);
            free(new_ranks);
            runtime_error(ctx, "tokenizer_add_special() out of memory");
            return val_null();
        }
        tok->special_tokens = new_tokens;
        tok->special_ranks = new_ranks;
        tok->special_capacity = new_cap;
    }

    tok->special_tokens[tok->num_special] = strdup(token_str);
    tok->special_ranks[tok->num_special] = rank;
    tok->num_special++;

    /* Also add to the vocab hash table for decoding */
    int token_len = (int)strlen(token_str);
    uint8_t *bytes_copy = malloc((size_t)token_len);
    if (bytes_copy) {
        memcpy(bytes_copy, token_str, (size_t)token_len);

        /* Grow vocab if needed */
        if (tok->vocab_size >= tok->vocab_capacity) {
            int32_t new_cap = tok->vocab_capacity * HML_GROWTH_FACTOR;
            BpeVocabEntry *new_vocab = realloc(tok->vocab, (size_t)new_cap * sizeof(BpeVocabEntry));
            if (new_vocab) {
                tok->vocab = new_vocab;
                tok->vocab_capacity = new_cap;
            }
        }

        if (tok->vocab_size < tok->vocab_capacity) {
            BpeVocabEntry *entry = &tok->vocab[tok->vocab_size];
            entry->bytes = bytes_copy;
            entry->byte_len = token_len;
            entry->rank = rank;
            tok->vocab_size++;

            bpe_hash_insert(tok, bytes_copy, token_len, rank);
        } else {
            free(bytes_copy);
        }
    }

    return val_null();
}
