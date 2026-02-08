/*
 * Hemlock Runtime - BPE Tokenizer Functions
 *
 * Runtime library implementation of BPE (Byte Pair Encoding) tokenizer
 * for compiled Hemlock programs. Supports tiktoken-compatible vocabulary files.
 */

#include "../include/hemlock_runtime.h"
#include "../include/hemlock_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ========== CONSTANTS ========== */

#define BPE_INITIAL_HASH_CAPACITY 131071
#define BPE_MAX_TOKEN_LENGTH 128
#define BPE_INITIAL_WORK_CAPACITY 256
#define BPE_BASE64_DECODE_BUFSIZE 256
#define BPE_MAX_VOCAB_LINE_LENGTH 1024
#define BPE_GROWTH_FACTOR 2

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

typedef struct {
    uint8_t *bytes;
    int byte_len;
    int32_t rank;
} BpeVocabEntry;

typedef struct {
    uint8_t *key;
    int key_len;
    int32_t value;
} BpeHashEntry;

typedef struct {
    BpeVocabEntry *vocab;
    int32_t vocab_size;
    int32_t vocab_capacity;
    BpeHashEntry *hash_table;
    int32_t hash_capacity;
    char **special_tokens;
    int32_t *special_ranks;
    int32_t num_special;
    int32_t special_capacity;
} BpeTokenizer;

/* ========== HASH TABLE ========== */

static uint32_t fnv1a_bytes(const uint8_t *data, int len) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void bpe_hash_init(BpeTokenizer *tok, int32_t capacity) {
    tok->hash_capacity = capacity;
    tok->hash_table = calloc((size_t)capacity, sizeof(BpeHashEntry));
    if (!tok->hash_table) return;
    for (int32_t i = 0; i < capacity; i++) {
        tok->hash_table[i].value = -1;
    }
}

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

static void bpe_tokenizer_destroy(BpeTokenizer *tok) {
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

static BpeTokenizer *bpe_tokenizer_load(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    BpeTokenizer *tok = calloc(1, sizeof(BpeTokenizer));
    if (!tok) { fclose(fp); return NULL; }

    tok->vocab_capacity = 4096;
    tok->vocab = calloc((size_t)tok->vocab_capacity, sizeof(BpeVocabEntry));
    if (!tok->vocab) { fclose(fp); bpe_tokenizer_destroy(tok); return NULL; }

    char line[BPE_MAX_VOCAB_LINE_LENGTH];

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '\n' || line[0] == '#') continue;
        char *space = strchr(line, ' ');
        if (!space) continue;
        *space = '\0';
        char *b64_str = line;
        int b64_len = (int)(space - line);
        int32_t rank = (int32_t)strtol(space + 1, NULL, 10);

        uint8_t decoded[BPE_BASE64_DECODE_BUFSIZE];
        int decoded_len = b64_decode(b64_str, b64_len, decoded, BPE_BASE64_DECODE_BUFSIZE);
        if (decoded_len <= 0) continue;

        if (tok->vocab_size >= tok->vocab_capacity) {
            int32_t new_cap = tok->vocab_capacity * BPE_GROWTH_FACTOR;
            BpeVocabEntry *new_vocab = realloc(tok->vocab, (size_t)new_cap * sizeof(BpeVocabEntry));
            if (!new_vocab) continue;
            tok->vocab = new_vocab;
            tok->vocab_capacity = new_cap;
        }

        uint8_t *bytes_copy = malloc((size_t)decoded_len);
        if (!bytes_copy) continue;
        memcpy(bytes_copy, decoded, (size_t)decoded_len);

        BpeVocabEntry *entry = &tok->vocab[tok->vocab_size];
        entry->bytes = bytes_copy;
        entry->byte_len = decoded_len;
        entry->rank = rank;
        tok->vocab_size++;
    }
    fclose(fp);

    if (tok->vocab_size == 0) { bpe_tokenizer_destroy(tok); return NULL; }

    int32_t hash_cap = tok->vocab_size * 3;
    if (hash_cap < BPE_INITIAL_HASH_CAPACITY) hash_cap = BPE_INITIAL_HASH_CAPACITY;
    bpe_hash_init(tok, hash_cap);

    for (int32_t i = 0; i < tok->vocab_size; i++) {
        bpe_hash_insert(tok, tok->vocab[i].bytes, tok->vocab[i].byte_len, tok->vocab[i].rank);
    }

    return tok;
}

/* ========== BPE ENCODE ========== */

typedef struct BpeNode {
    uint8_t *bytes;
    int byte_len;
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

    int node_capacity = data_len + 1;
    BpeNode *nodes = calloc((size_t)node_capacity, sizeof(BpeNode));
    if (!nodes) return -1;

    uint8_t *data_copy = malloc((size_t)data_len);
    if (!data_copy) { free(nodes); return -1; }
    memcpy(data_copy, data, (size_t)data_len);

    for (int i = 0; i < data_len; i++) {
        nodes[i].bytes = data_copy + i;
        nodes[i].byte_len = 1;
        nodes[i].prev = (i > 0) ? &nodes[i - 1] : NULL;
        nodes[i].next = (i < data_len - 1) ? &nodes[i + 1] : NULL;
    }

    uint8_t pair_buf[BPE_MAX_TOKEN_LENGTH * 2];

    int changed = 1;
    while (changed) {
        changed = 0;
        int32_t best_rank = INT32_MAX;
        BpeNode *best_node = NULL;

        for (BpeNode *node = &nodes[0]; node && node->next; node = node->next) {
            if (node->byte_len <= 0) continue;
            BpeNode *next = node->next;
            int pair_len = node->byte_len + next->byte_len;
            if (pair_len > BPE_MAX_TOKEN_LENGTH * 2) continue;

            memcpy(pair_buf, node->bytes, (size_t)node->byte_len);
            memcpy(pair_buf + node->byte_len, next->bytes, (size_t)next->byte_len);

            int32_t rank = bpe_hash_lookup(tok, pair_buf, pair_len);
            if (rank >= 0 && rank < best_rank) {
                best_rank = rank;
                best_node = node;
            }
        }

        if (best_node) {
            BpeNode *next = best_node->next;
            int new_len = best_node->byte_len + next->byte_len;

            uint8_t *merged = malloc((size_t)new_len);
            if (!merged) break;
            memcpy(merged, best_node->bytes, (size_t)best_node->byte_len);
            memcpy(merged + best_node->byte_len, next->bytes, (size_t)next->byte_len);

            if (best_node->bytes < data_copy || best_node->bytes >= data_copy + data_len) {
                free(best_node->bytes);
            }

            best_node->bytes = merged;
            best_node->byte_len = new_len;

            best_node->next = next->next;
            if (next->next) next->next->prev = best_node;

            if (next->bytes < data_copy || next->bytes >= data_copy + data_len) {
                free(next->bytes);
            }
            next->bytes = NULL;
            next->byte_len = 0;

            changed = 1;
        }
    }

    int count = 0;
    for (BpeNode *node = &nodes[0]; node; node = node->next) {
        if (node->byte_len > 0) count++;
    }

    int32_t *tokens = malloc((size_t)count * sizeof(int32_t));
    if (!tokens) {
        for (int i = 0; i < node_capacity; i++) {
            if (nodes[i].bytes && (nodes[i].bytes < data_copy || nodes[i].bytes >= data_copy + data_len)) {
                free(nodes[i].bytes);
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
            for (int b = 0; b < node->byte_len; b++) {
                int32_t byte_rank = bpe_hash_lookup(tok, &node->bytes[b], 1);
                if (byte_rank >= 0) {
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

    for (int i = 0; i < node_capacity; i++) {
        if (nodes[i].bytes && (nodes[i].bytes < data_copy || nodes[i].bytes >= data_copy + data_len)) {
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

/* ========== PRE-TOKENIZER ========== */

typedef struct {
    const uint8_t *start;
    int len;
} TextChunk;

static int is_letter_or_digit(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c >= 128;
}

static int pretokenize(const uint8_t *text, int text_len,
                       TextChunk **out_chunks, int *out_count) {
    if (text_len == 0) {
        *out_chunks = NULL;
        *out_count = 0;
        return 0;
    }

    int capacity = BPE_INITIAL_WORK_CAPACITY;
    TextChunk *chunks = malloc((size_t)capacity * sizeof(TextChunk));
    if (!chunks) return -1;
    int count = 0;

    int i = 0;
    while (i < text_len) {
        int start = i;
        while (i < text_len && (text[i] == ' ' || text[i] == '\t')) i++;
        if (i < text_len) {
            if (text[i] == '\n' || text[i] == '\r') {
                i++;
                if (i < text_len && text[i - 1] == '\r' && text[i] == '\n') i++;
            } else if (is_letter_or_digit(text[i])) {
                while (i < text_len && is_letter_or_digit(text[i])) i++;
            } else {
                i++;
            }
        }
        if (i > start) {
            if (count >= capacity) {
                capacity *= BPE_GROWTH_FACTOR;
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

static int bpe_encode(BpeTokenizer *tok, const char *text, int text_len,
                      int32_t **out_tokens, int *out_count) {
    TextChunk *chunks = NULL;
    int num_chunks = 0;
    if (pretokenize((const uint8_t *)text, text_len, &chunks, &num_chunks) != 0) {
        return -1;
    }

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

        while (total_count + chunk_count > total_capacity) {
            total_capacity *= BPE_GROWTH_FACTOR;
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
    int total_len = 0;
    for (int i = 0; i < num_tokens; i++) {
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

/* ========== RUNTIME API FUNCTIONS ========== */

HmlValue hml_tokenizer_create(HmlValue path) {
    if (path.type != HML_VAL_STRING || !path.as.as_string) {
        hml_runtime_error("tokenizer_create() requires string path argument");
    }
    BpeTokenizer *tok = bpe_tokenizer_load(path.as.as_string->data);
    if (!tok) {
        hml_runtime_error("tokenizer_create() failed to load vocabulary from: %s", path.as.as_string->data);
    }
    return hml_val_ptr(tok);
}

HmlValue hml_tokenizer_free(HmlValue handle) {
    if (handle.type != HML_VAL_PTR) {
        hml_runtime_error("tokenizer_free() requires ptr argument");
    }
    bpe_tokenizer_destroy((BpeTokenizer *)handle.as.as_ptr);
    return hml_val_null();
}

HmlValue hml_tokenizer_encode(HmlValue handle, HmlValue text) {
    if (handle.type != HML_VAL_PTR || !handle.as.as_ptr) {
        hml_runtime_error("tokenizer_encode() requires valid tokenizer ptr");
    }
    if (text.type != HML_VAL_STRING || !text.as.as_string) {
        hml_runtime_error("tokenizer_encode() requires string text argument");
    }

    BpeTokenizer *tok = (BpeTokenizer *)handle.as.as_ptr;
    const char *str = text.as.as_string->data;
    int str_len = text.as.as_string->length;

    int32_t *tokens = NULL;
    int num_tokens = 0;
    if (bpe_encode(tok, str, str_len, &tokens, &num_tokens) != 0) {
        hml_runtime_error("tokenizer_encode() encoding failed");
    }

    HmlValue result = hml_val_array();
    for (int i = 0; i < num_tokens; i++) {
        HmlValue v;
        v.type = HML_VAL_I32;
        v.as.as_i32 = tokens[i];
        hml_array_push(result, v);
    }
    free(tokens);
    return result;
}

HmlValue hml_tokenizer_decode(HmlValue handle, HmlValue tokens_arr) {
    if (handle.type != HML_VAL_PTR || !handle.as.as_ptr) {
        hml_runtime_error("tokenizer_decode() requires valid tokenizer ptr");
    }
    if (tokens_arr.type != HML_VAL_ARRAY || !tokens_arr.as.as_array) {
        hml_runtime_error("tokenizer_decode() requires array argument");
    }

    BpeTokenizer *tok = (BpeTokenizer *)handle.as.as_ptr;
    HmlArray *arr = tokens_arr.as.as_array;
    int count = arr->length;

    int32_t *token_ids = malloc((size_t)count * sizeof(int32_t));
    if (!token_ids && count > 0) {
        hml_runtime_error("tokenizer_decode() out of memory");
    }

    for (int i = 0; i < count; i++) {
        token_ids[i] = (int32_t)hml_to_i64(arr->elements[i]);
    }

    int result_len = 0;
    char *decoded = bpe_decode(tok, token_ids, count, &result_len);
    free(token_ids);

    if (!decoded) {
        hml_runtime_error("tokenizer_decode() decode failed");
    }

    HmlValue result = hml_val_string(decoded);
    free(decoded);
    return result;
}

HmlValue hml_tokenizer_count(HmlValue handle, HmlValue text) {
    if (handle.type != HML_VAL_PTR || !handle.as.as_ptr) {
        hml_runtime_error("tokenizer_count() requires valid tokenizer ptr");
    }
    if (text.type != HML_VAL_STRING || !text.as.as_string) {
        hml_runtime_error("tokenizer_count() requires string text argument");
    }

    BpeTokenizer *tok = (BpeTokenizer *)handle.as.as_ptr;
    const char *str = text.as.as_string->data;
    int str_len = text.as.as_string->length;

    int32_t *tokens = NULL;
    int num_tokens = 0;
    if (bpe_encode(tok, str, str_len, &tokens, &num_tokens) != 0) {
        hml_runtime_error("tokenizer_count() encoding failed");
    }
    free(tokens);

    HmlValue result;
    result.type = HML_VAL_I32;
    result.as.as_i32 = num_tokens;
    return result;
}

HmlValue hml_tokenizer_vocab_size(HmlValue handle) {
    if (handle.type != HML_VAL_PTR || !handle.as.as_ptr) {
        HmlValue result;
        result.type = HML_VAL_I32;
        result.as.as_i32 = 0;
        return result;
    }
    BpeTokenizer *tok = (BpeTokenizer *)handle.as.as_ptr;
    HmlValue result;
    result.type = HML_VAL_I32;
    result.as.as_i32 = tok->vocab_size;
    return result;
}

HmlValue hml_tokenizer_add_special(HmlValue handle, HmlValue token, HmlValue rank) {
    if (handle.type != HML_VAL_PTR || !handle.as.as_ptr) {
        hml_runtime_error("tokenizer_add_special() requires valid tokenizer ptr");
    }
    if (token.type != HML_VAL_STRING || !token.as.as_string) {
        hml_runtime_error("tokenizer_add_special() requires string token argument");
    }

    BpeTokenizer *tok = (BpeTokenizer *)handle.as.as_ptr;
    const char *token_str = token.as.as_string->data;
    int32_t rank_val = (int32_t)hml_to_i64(rank);
    int token_len = token.as.as_string->length;

    if (tok->num_special >= tok->special_capacity) {
        int32_t new_cap = tok->special_capacity == 0 ? 16 : tok->special_capacity * BPE_GROWTH_FACTOR;
        char **new_tokens = realloc(tok->special_tokens, (size_t)new_cap * sizeof(char *));
        int32_t *new_ranks = realloc(tok->special_ranks, (size_t)new_cap * sizeof(int32_t));
        if (!new_tokens || !new_ranks) {
            free(new_tokens);
            free(new_ranks);
            hml_runtime_error("tokenizer_add_special() out of memory");
        }
        tok->special_tokens = new_tokens;
        tok->special_ranks = new_ranks;
        tok->special_capacity = new_cap;
    }

    tok->special_tokens[tok->num_special] = strdup(token_str);
    tok->special_ranks[tok->num_special] = rank_val;
    tok->num_special++;

    uint8_t *bytes_copy = malloc((size_t)token_len);
    if (bytes_copy) {
        memcpy(bytes_copy, token_str, (size_t)token_len);

        if (tok->vocab_size >= tok->vocab_capacity) {
            int32_t new_cap = tok->vocab_capacity * BPE_GROWTH_FACTOR;
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
            entry->rank = rank_val;
            tok->vocab_size++;
            bpe_hash_insert(tok, bytes_copy, token_len, rank_val);
        } else {
            free(bytes_copy);
        }
    }

    return hml_val_null();
}
