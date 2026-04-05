/*
 * Hemlock Runtime Library - Byte Order Builtins
 *
 * Provides byte swapping and endianness conversion utilities
 * for the compiled backend.
 */

#include "builtins_internal.h"

// ========== CORE BYTE SWAP FUNCTIONS ==========

HmlValue hml_bswap16(HmlValue val) {
    uint16_t v = (uint16_t)hml_to_i32(val);
    uint16_t swapped = (uint16_t)((v >> 8) | (v << 8));
    return hml_val_u16(swapped);
}

HmlValue hml_bswap32(HmlValue val) {
    uint32_t v = (uint32_t)hml_to_i32(val);
    uint32_t swapped = __builtin_bswap32(v);
    return hml_val_u32(swapped);
}

HmlValue hml_bswap64(HmlValue val) {
    uint64_t v = (uint64_t)hml_to_i64(val);
    uint64_t swapped = __builtin_bswap64(v);
    return hml_val_u64(swapped);
}

// ========== HOST TO NETWORK BYTE ORDER ==========
// Implemented via byte swaps instead of htons/htonl to avoid
// dependency on arpa/inet.h (not available in WASM builds).

HmlValue hml_htons_val(HmlValue val) {
    uint16_t v = (uint16_t)hml_to_i32(val);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t result = (uint16_t)((v >> 8) | (v << 8));
#else
    uint16_t result = v;
#endif
    return hml_val_u16(result);
}

HmlValue hml_htonl_val(HmlValue val) {
    uint32_t v = (uint32_t)hml_to_i32(val);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t result = __builtin_bswap32(v);
#else
    uint32_t result = v;
#endif
    return hml_val_u32(result);
}

HmlValue hml_htonll_val(HmlValue val) {
    uint64_t v = (uint64_t)hml_to_i64(val);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t result = __builtin_bswap64(v);
#else
    uint64_t result = v;
#endif
    return hml_val_u64(result);
}

// ========== NETWORK TO HOST BYTE ORDER ==========

HmlValue hml_ntohs_val(HmlValue val) {
    uint16_t v = (uint16_t)hml_to_i32(val);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint16_t result = (uint16_t)((v >> 8) | (v << 8));
#else
    uint16_t result = v;
#endif
    return hml_val_u16(result);
}

HmlValue hml_ntohl_val(HmlValue val) {
    uint32_t v = (uint32_t)hml_to_i32(val);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t result = __builtin_bswap32(v);
#else
    uint32_t result = v;
#endif
    return hml_val_u32(result);
}

HmlValue hml_ntohll_val(HmlValue val) {
    uint64_t v = (uint64_t)hml_to_i64(val);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t result = __builtin_bswap64(v);
#else
    uint64_t result = v;
#endif
    return hml_val_u64(result);
}

// ========== ENDIANNESS QUERY ==========

HmlValue hml_is_little_endian(void) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return hml_val_bool(1);
#else
    return hml_val_bool(0);
#endif
}

// ========== ENDIAN-AWARE BUFFER READ ==========

static inline void *extract_ptr(HmlValue val) {
    if (val.type == HML_VAL_PTR) return val.as.as_ptr;
    if (val.type == HML_VAL_BUFFER && val.as.as_buffer) return val.as.as_buffer->data;
    return NULL;
}

HmlValue hml_read_u16_be(HmlValue ptr, HmlValue offset_val) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("read_u16_be() cannot read from null pointer");
    int offset = hml_to_i32(offset_val);
    uint8_t *bytes = (uint8_t *)p + offset;
    uint16_t result = ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
    return hml_val_u16(result);
}

HmlValue hml_read_u16_le(HmlValue ptr, HmlValue offset_val) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("read_u16_le() cannot read from null pointer");
    int offset = hml_to_i32(offset_val);
    uint8_t *bytes = (uint8_t *)p + offset;
    uint16_t result = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return hml_val_u16(result);
}

HmlValue hml_read_u32_be(HmlValue ptr, HmlValue offset_val) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("read_u32_be() cannot read from null pointer");
    int offset = hml_to_i32(offset_val);
    uint8_t *bytes = (uint8_t *)p + offset;
    uint32_t result = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                      ((uint32_t)bytes[2] << 8)  | (uint32_t)bytes[3];
    return hml_val_u32(result);
}

HmlValue hml_read_u32_le(HmlValue ptr, HmlValue offset_val) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("read_u32_le() cannot read from null pointer");
    int offset = hml_to_i32(offset_val);
    uint8_t *bytes = (uint8_t *)p + offset;
    uint32_t result = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                      ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return hml_val_u32(result);
}

HmlValue hml_read_u64_be(HmlValue ptr, HmlValue offset_val) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("read_u64_be() cannot read from null pointer");
    int offset = hml_to_i32(offset_val);
    uint8_t *bytes = (uint8_t *)p + offset;
    uint64_t result = ((uint64_t)bytes[0] << 56) | ((uint64_t)bytes[1] << 48) |
                      ((uint64_t)bytes[2] << 40) | ((uint64_t)bytes[3] << 32) |
                      ((uint64_t)bytes[4] << 24) | ((uint64_t)bytes[5] << 16) |
                      ((uint64_t)bytes[6] << 8)  | (uint64_t)bytes[7];
    return hml_val_u64(result);
}

HmlValue hml_read_u64_le(HmlValue ptr, HmlValue offset_val) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("read_u64_le() cannot read from null pointer");
    int offset = hml_to_i32(offset_val);
    uint8_t *bytes = (uint8_t *)p + offset;
    uint64_t result = (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8) |
                      ((uint64_t)bytes[2] << 16) | ((uint64_t)bytes[3] << 24) |
                      ((uint64_t)bytes[4] << 32) | ((uint64_t)bytes[5] << 40) |
                      ((uint64_t)bytes[6] << 48) | ((uint64_t)bytes[7] << 56);
    return hml_val_u64(result);
}

// ========== ENDIAN-AWARE BUFFER WRITE ==========

HmlValue hml_write_u16_be(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("write_u16_be() cannot write to null pointer");
    int offset = hml_to_i32(offset_val);
    uint16_t val = (uint16_t)hml_to_i32(value);
    uint8_t *bytes = (uint8_t *)p + offset;
    bytes[0] = (uint8_t)(val >> 8);
    bytes[1] = (uint8_t)(val & 0xFF);
    return hml_val_null();
}

HmlValue hml_write_u16_le(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("write_u16_le() cannot write to null pointer");
    int offset = hml_to_i32(offset_val);
    uint16_t val = (uint16_t)hml_to_i32(value);
    uint8_t *bytes = (uint8_t *)p + offset;
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    return hml_val_null();
}

HmlValue hml_write_u32_be(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("write_u32_be() cannot write to null pointer");
    int offset = hml_to_i32(offset_val);
    uint32_t val = (uint32_t)hml_to_i32(value);
    uint8_t *bytes = (uint8_t *)p + offset;
    bytes[0] = (uint8_t)(val >> 24);
    bytes[1] = (uint8_t)(val >> 16);
    bytes[2] = (uint8_t)(val >> 8);
    bytes[3] = (uint8_t)(val & 0xFF);
    return hml_val_null();
}

HmlValue hml_write_u32_le(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("write_u32_le() cannot write to null pointer");
    int offset = hml_to_i32(offset_val);
    uint32_t val = (uint32_t)hml_to_i32(value);
    uint8_t *bytes = (uint8_t *)p + offset;
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    bytes[2] = (uint8_t)(val >> 16);
    bytes[3] = (uint8_t)(val >> 24);
    return hml_val_null();
}

HmlValue hml_write_u64_be(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("write_u64_be() cannot write to null pointer");
    int offset = hml_to_i32(offset_val);
    uint64_t val = (uint64_t)hml_to_i64(value);
    uint8_t *bytes = (uint8_t *)p + offset;
    bytes[0] = (uint8_t)(val >> 56);
    bytes[1] = (uint8_t)(val >> 48);
    bytes[2] = (uint8_t)(val >> 40);
    bytes[3] = (uint8_t)(val >> 32);
    bytes[4] = (uint8_t)(val >> 24);
    bytes[5] = (uint8_t)(val >> 16);
    bytes[6] = (uint8_t)(val >> 8);
    bytes[7] = (uint8_t)(val & 0xFF);
    return hml_val_null();
}

HmlValue hml_write_u64_le(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    void *p = extract_ptr(ptr);
    if (!p) hml_runtime_error("write_u64_le() cannot write to null pointer");
    int offset = hml_to_i32(offset_val);
    uint64_t val = (uint64_t)hml_to_i64(value);
    uint8_t *bytes = (uint8_t *)p + offset;
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    bytes[2] = (uint8_t)(val >> 16);
    bytes[3] = (uint8_t)(val >> 24);
    bytes[4] = (uint8_t)(val >> 32);
    bytes[5] = (uint8_t)(val >> 40);
    bytes[6] = (uint8_t)(val >> 48);
    bytes[7] = (uint8_t)(val >> 56);
    return hml_val_null();
}

// ========== BUILTIN WRAPPERS (for first-class function references) ==========

HmlValue hml_builtin_bswap16(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_bswap16(val);
}

HmlValue hml_builtin_bswap32(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_bswap32(val);
}

HmlValue hml_builtin_bswap64(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_bswap64(val);
}

HmlValue hml_builtin_htons(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_htons_val(val);
}

HmlValue hml_builtin_htonl(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_htonl_val(val);
}

HmlValue hml_builtin_htonll(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_htonll_val(val);
}

HmlValue hml_builtin_ntohs(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_ntohs_val(val);
}

HmlValue hml_builtin_ntohl(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_ntohl_val(val);
}

HmlValue hml_builtin_ntohll(HmlClosureEnv *env, HmlValue val) {
    (void)env;
    return hml_ntohll_val(val);
}

HmlValue hml_builtin_is_little_endian(HmlClosureEnv *env) {
    (void)env;
    return hml_is_little_endian();
}

HmlValue hml_builtin_read_u16_be(HmlClosureEnv *env, HmlValue ptr, HmlValue offset) {
    (void)env;
    return hml_read_u16_be(ptr, offset);
}

HmlValue hml_builtin_read_u16_le(HmlClosureEnv *env, HmlValue ptr, HmlValue offset) {
    (void)env;
    return hml_read_u16_le(ptr, offset);
}

HmlValue hml_builtin_read_u32_be(HmlClosureEnv *env, HmlValue ptr, HmlValue offset) {
    (void)env;
    return hml_read_u32_be(ptr, offset);
}

HmlValue hml_builtin_read_u32_le(HmlClosureEnv *env, HmlValue ptr, HmlValue offset) {
    (void)env;
    return hml_read_u32_le(ptr, offset);
}

HmlValue hml_builtin_read_u64_be(HmlClosureEnv *env, HmlValue ptr, HmlValue offset) {
    (void)env;
    return hml_read_u64_be(ptr, offset);
}

HmlValue hml_builtin_read_u64_le(HmlClosureEnv *env, HmlValue ptr, HmlValue offset) {
    (void)env;
    return hml_read_u64_le(ptr, offset);
}

HmlValue hml_builtin_write_u16_be(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue value) {
    (void)env;
    return hml_write_u16_be(ptr, offset, value);
}

HmlValue hml_builtin_write_u16_le(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue value) {
    (void)env;
    return hml_write_u16_le(ptr, offset, value);
}

HmlValue hml_builtin_write_u32_be(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue value) {
    (void)env;
    return hml_write_u32_be(ptr, offset, value);
}

HmlValue hml_builtin_write_u32_le(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue value) {
    (void)env;
    return hml_write_u32_le(ptr, offset, value);
}

HmlValue hml_builtin_write_u64_be(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue value) {
    (void)env;
    return hml_write_u64_be(ptr, offset, value);
}

HmlValue hml_builtin_write_u64_le(HmlClosureEnv *env, HmlValue ptr, HmlValue offset, HmlValue value) {
    (void)env;
    return hml_write_u64_le(ptr, offset, value);
}
