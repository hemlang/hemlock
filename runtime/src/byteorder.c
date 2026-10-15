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

// Resolve a ptr/buffer value to a byte address at `offset`, validating bounds
// for safe buffers. Raw ptr is intentionally unchecked (Hemlock's documented
// unsafe-pointer contract). For buffers the access window [offset, offset+size)
// is validated against the buffer length using 64-bit math so a huge/negative
// offset cannot wrap an int and bypass the check, and freed buffers are
// rejected. This is what stops @stdlib/bytes read_*/write_* from being used as
// an arbitrary heap read/write primitive.
static inline uint8_t *byteorder_addr(HmlValue val, int offset, int access_size, const char *op) {
    if (val.type == HML_VAL_PTR) {
        if (!val.as.as_ptr) hml_runtime_error("%s cannot access null pointer", op);
        return (uint8_t *)val.as.as_ptr + offset;
    }
    if (val.type == HML_VAL_BUFFER && val.as.as_buffer) {
        HmlBuffer *b = val.as.as_buffer;
        if (b->freed) hml_runtime_error("%s cannot access freed buffer", op);
        if (offset < 0 || (int64_t)offset + (int64_t)access_size > (int64_t)b->length) {
            hml_runtime_error("%s offset %d with size %d out of bounds (buffer length %d)",
                              op, offset, access_size, b->length);
        }
        if (!b->data) hml_runtime_error("%s cannot access null buffer", op);
        return (uint8_t *)b->data + offset;
    }
    hml_runtime_error("%s requires a ptr or buffer", op);
    return NULL;  // unreachable: hml_runtime_error does not return
}

HmlValue hml_read_u16_be(HmlValue ptr, HmlValue offset_val) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 2, "read_u16_be()");
    uint16_t result = ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
    return hml_val_u16(result);
}

HmlValue hml_read_u16_le(HmlValue ptr, HmlValue offset_val) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 2, "read_u16_le()");
    uint16_t result = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return hml_val_u16(result);
}

HmlValue hml_read_u32_be(HmlValue ptr, HmlValue offset_val) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 4, "read_u32_be()");
    uint32_t result = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                      ((uint32_t)bytes[2] << 8)  | (uint32_t)bytes[3];
    return hml_val_u32(result);
}

HmlValue hml_read_u32_le(HmlValue ptr, HmlValue offset_val) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 4, "read_u32_le()");
    uint32_t result = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                      ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return hml_val_u32(result);
}

HmlValue hml_read_u64_be(HmlValue ptr, HmlValue offset_val) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 8, "read_u64_be()");
    uint64_t result = ((uint64_t)bytes[0] << 56) | ((uint64_t)bytes[1] << 48) |
                      ((uint64_t)bytes[2] << 40) | ((uint64_t)bytes[3] << 32) |
                      ((uint64_t)bytes[4] << 24) | ((uint64_t)bytes[5] << 16) |
                      ((uint64_t)bytes[6] << 8)  | (uint64_t)bytes[7];
    return hml_val_u64(result);
}

HmlValue hml_read_u64_le(HmlValue ptr, HmlValue offset_val) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 8, "read_u64_le()");
    uint64_t result = (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8) |
                      ((uint64_t)bytes[2] << 16) | ((uint64_t)bytes[3] << 24) |
                      ((uint64_t)bytes[4] << 32) | ((uint64_t)bytes[5] << 40) |
                      ((uint64_t)bytes[6] << 48) | ((uint64_t)bytes[7] << 56);
    return hml_val_u64(result);
}

// ========== ENDIAN-AWARE BUFFER WRITE ==========

HmlValue hml_write_u16_be(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 2, "write_u16_be()");
    uint16_t val = (uint16_t)hml_to_i32(value);
    bytes[0] = (uint8_t)(val >> 8);
    bytes[1] = (uint8_t)(val & 0xFF);
    return hml_val_null();
}

HmlValue hml_write_u16_le(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 2, "write_u16_le()");
    uint16_t val = (uint16_t)hml_to_i32(value);
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    return hml_val_null();
}

HmlValue hml_write_u32_be(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 4, "write_u32_be()");
    uint32_t val = (uint32_t)hml_to_i32(value);
    bytes[0] = (uint8_t)(val >> 24);
    bytes[1] = (uint8_t)(val >> 16);
    bytes[2] = (uint8_t)(val >> 8);
    bytes[3] = (uint8_t)(val & 0xFF);
    return hml_val_null();
}

HmlValue hml_write_u32_le(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 4, "write_u32_le()");
    uint32_t val = (uint32_t)hml_to_i32(value);
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    bytes[2] = (uint8_t)(val >> 16);
    bytes[3] = (uint8_t)(val >> 24);
    return hml_val_null();
}

HmlValue hml_write_u64_be(HmlValue ptr, HmlValue offset_val, HmlValue value) {
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 8, "write_u64_be()");
    uint64_t val = (uint64_t)hml_to_i64(value);
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
    uint8_t *bytes = byteorder_addr(ptr, hml_to_i32(offset_val), 8, "write_u64_le()");
    uint64_t val = (uint64_t)hml_to_i64(value);
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
