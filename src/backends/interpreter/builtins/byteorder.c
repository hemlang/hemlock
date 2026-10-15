/*
 * Hemlock Interpreter - Byte Order Builtins
 *
 * Provides byte swapping and endianness conversion utilities:
 * - bswap16, bswap32, bswap64: raw byte swap
 * - htons, htonl, htonll: host to network byte order
 * - ntohs, ntohl, ntohll: network to host byte order
 * - is_little_endian: query system endianness
 */

#include "internal.h"

// ========== BYTE SWAP BUILTINS ==========

// bswap16(val: u16): u16 - Swap bytes of a 16-bit value
Value builtin_bswap16(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "bswap16() expects 1 argument");
        return val_null();
    }
    uint16_t val = (uint16_t)value_to_int(args[0]);
    uint16_t swapped = (uint16_t)((val >> 8) | (val << 8));
    return val_u16(swapped);
}

// bswap32(val: u32): u32 - Swap bytes of a 32-bit value
Value builtin_bswap32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "bswap32() expects 1 argument");
        return val_null();
    }
    uint32_t val = (uint32_t)value_to_int(args[0]);
    uint32_t swapped = __builtin_bswap32(val);
    return val_u32(swapped);
}

// bswap64(val: u64): u64 - Swap bytes of a 64-bit value
Value builtin_bswap64(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "bswap64() expects 1 argument");
        return val_null();
    }
    uint64_t val = (uint64_t)value_to_int64(args[0]);
    uint64_t swapped = __builtin_bswap64(val);
    return val_u64(swapped);
}

// ========== HOST TO NETWORK BYTE ORDER ==========

// htons(val: u16): u16 - Host to network byte order (16-bit)
Value builtin_htons(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "htons() expects 1 argument");
        return val_null();
    }
    uint16_t val = (uint16_t)value_to_int(args[0]);
    uint16_t result = htons(val);
    return val_u16(result);
}

// htonl(val: u32): u32 - Host to network byte order (32-bit)
Value builtin_htonl(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "htonl() expects 1 argument");
        return val_null();
    }
    uint32_t val = (uint32_t)value_to_int(args[0]);
    uint32_t result = htonl(val);
    return val_u32(result);
}

// htonll(val: u64): u64 - Host to network byte order (64-bit)
Value builtin_htonll(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "htonll() expects 1 argument");
        return val_null();
    }
    uint64_t val = (uint64_t)value_to_int64(args[0]);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t result = __builtin_bswap64(val);
#else
    uint64_t result = val;
#endif
    return val_u64(result);
}

// ========== NETWORK TO HOST BYTE ORDER ==========

// ntohs(val: u16): u16 - Network to host byte order (16-bit)
Value builtin_ntohs(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ntohs() expects 1 argument");
        return val_null();
    }
    uint16_t val = (uint16_t)value_to_int(args[0]);
    uint16_t result = ntohs(val);
    return val_u16(result);
}

// ntohl(val: u32): u32 - Network to host byte order (32-bit)
Value builtin_ntohl(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ntohl() expects 1 argument");
        return val_null();
    }
    uint32_t val = (uint32_t)value_to_int(args[0]);
    uint32_t result = ntohl(val);
    return val_u32(result);
}

// ntohll(val: u64): u64 - Network to host byte order (64-bit)
Value builtin_ntohll(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "ntohll() expects 1 argument");
        return val_null();
    }
    uint64_t val = (uint64_t)value_to_int64(args[0]);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t result = __builtin_bswap64(val);
#else
    uint64_t result = val;
#endif
    return val_u64(result);
}

// ========== ENDIANNESS QUERY ==========

// is_little_endian(): bool - Returns true if system is little-endian
Value builtin_is_little_endian(Value *args, int num_args, ExecutionContext *ctx) {
    (void)args;
    (void)ctx;
    if (num_args != 0) {
        runtime_error(ctx, "is_little_endian() expects 0 arguments");
        return val_null();
    }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return val_bool(1);
#else
    return val_bool(0);
#endif
}

// ========== ENDIAN-AWARE BUFFER READ/WRITE ==========

// Resolve a ptr/buffer argument to a byte address at `offset`. Raw ptr is
// unchecked (Hemlock's documented unsafe-pointer contract); buffers are
// bounds-checked with 64-bit math so a huge/negative offset cannot be used to
// read or write outside the allocation via these endian helpers. On failure it
// raises a catchable error and returns NULL. Because runtime_error() does not
// abort execution, the caller MUST return immediately when NULL is returned.
static uint8_t *byteorder_addr(Value arg, int offset, int access_size,
                               const char *op, ExecutionContext *ctx) {
    if (arg.type == VAL_PTR) {
        if (!arg.as.as_ptr) {
            runtime_error(ctx, "%s cannot access null pointer", op);
            return NULL;
        }
        return (uint8_t *)arg.as.as_ptr + offset;
    }
    if (arg.type == VAL_BUFFER && arg.as.as_buffer) {
        Buffer *b = arg.as.as_buffer;
        if (b->freed) {
            runtime_error(ctx, "%s cannot access freed buffer", op);
            return NULL;
        }
        if (offset < 0 || (int64_t)offset + (int64_t)access_size > (int64_t)b->length) {
            runtime_error(ctx, "%s offset %d with size %d out of bounds (buffer length %d)",
                          op, offset, access_size, b->length);
            return NULL;
        }
        if (!b->data) {
            runtime_error(ctx, "%s cannot access null buffer", op);
            return NULL;
        }
        return (uint8_t *)b->data + offset;
    }
    runtime_error(ctx, "%s requires a ptr or buffer", op);
    return NULL;
}

// read_u16_be(ptr, offset): u16 - Read big-endian u16 from pointer+offset
Value builtin_read_u16_be(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "read_u16_be() expects 2 arguments (ptr, offset)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "read_u16_be() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 2, "read_u16_be()", ctx);
    if (!bytes) return val_null();
    uint16_t result = ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
    return val_u16(result);
}

// read_u16_le(ptr, offset): u16 - Read little-endian u16 from pointer+offset
Value builtin_read_u16_le(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "read_u16_le() expects 2 arguments (ptr, offset)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "read_u16_le() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 2, "read_u16_le()", ctx);
    if (!bytes) return val_null();
    uint16_t result = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return val_u16(result);
}

// read_u32_be(ptr, offset): u32 - Read big-endian u32 from pointer+offset
Value builtin_read_u32_be(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "read_u32_be() expects 2 arguments (ptr, offset)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "read_u32_be() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 4, "read_u32_be()", ctx);
    if (!bytes) return val_null();
    uint32_t result = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                      ((uint32_t)bytes[2] << 8)  | (uint32_t)bytes[3];
    return val_u32(result);
}

// read_u32_le(ptr, offset): u32 - Read little-endian u32 from pointer+offset
Value builtin_read_u32_le(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "read_u32_le() expects 2 arguments (ptr, offset)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "read_u32_le() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 4, "read_u32_le()", ctx);
    if (!bytes) return val_null();
    uint32_t result = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                      ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return val_u32(result);
}

// read_u64_be(ptr, offset): u64 - Read big-endian u64 from pointer+offset
Value builtin_read_u64_be(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "read_u64_be() expects 2 arguments (ptr, offset)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "read_u64_be() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 8, "read_u64_be()", ctx);
    if (!bytes) return val_null();
    uint64_t result = ((uint64_t)bytes[0] << 56) | ((uint64_t)bytes[1] << 48) |
                      ((uint64_t)bytes[2] << 40) | ((uint64_t)bytes[3] << 32) |
                      ((uint64_t)bytes[4] << 24) | ((uint64_t)bytes[5] << 16) |
                      ((uint64_t)bytes[6] << 8)  | (uint64_t)bytes[7];
    return val_u64(result);
}

// read_u64_le(ptr, offset): u64 - Read little-endian u64 from pointer+offset
Value builtin_read_u64_le(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "read_u64_le() expects 2 arguments (ptr, offset)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "read_u64_le() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 8, "read_u64_le()", ctx);
    if (!bytes) return val_null();
    uint64_t result = (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8) |
                      ((uint64_t)bytes[2] << 16) | ((uint64_t)bytes[3] << 24) |
                      ((uint64_t)bytes[4] << 32) | ((uint64_t)bytes[5] << 40) |
                      ((uint64_t)bytes[6] << 48) | ((uint64_t)bytes[7] << 56);
    return val_u64(result);
}

// write_u16_be(ptr, offset, val): void - Write u16 as big-endian to pointer+offset
Value builtin_write_u16_be(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "write_u16_be() expects 3 arguments (ptr, offset, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "write_u16_be() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint16_t val = (uint16_t)value_to_int(args[2]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 2, "write_u16_be()", ctx);
    if (!bytes) return val_null();
    bytes[0] = (uint8_t)(val >> 8);
    bytes[1] = (uint8_t)(val & 0xFF);
    return val_null();
}

// write_u16_le(ptr, offset, val): void - Write u16 as little-endian to pointer+offset
Value builtin_write_u16_le(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "write_u16_le() expects 3 arguments (ptr, offset, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "write_u16_le() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint16_t val = (uint16_t)value_to_int(args[2]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 2, "write_u16_le()", ctx);
    if (!bytes) return val_null();
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    return val_null();
}

// write_u32_be(ptr, offset, val): void - Write u32 as big-endian to pointer+offset
Value builtin_write_u32_be(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "write_u32_be() expects 3 arguments (ptr, offset, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "write_u32_be() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint32_t val = (uint32_t)value_to_int(args[2]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 4, "write_u32_be()", ctx);
    if (!bytes) return val_null();
    bytes[0] = (uint8_t)(val >> 24);
    bytes[1] = (uint8_t)(val >> 16);
    bytes[2] = (uint8_t)(val >> 8);
    bytes[3] = (uint8_t)(val & 0xFF);
    return val_null();
}

// write_u32_le(ptr, offset, val): void - Write u32 as little-endian to pointer+offset
Value builtin_write_u32_le(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "write_u32_le() expects 3 arguments (ptr, offset, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "write_u32_le() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint32_t val = (uint32_t)value_to_int(args[2]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 4, "write_u32_le()", ctx);
    if (!bytes) return val_null();
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    bytes[2] = (uint8_t)(val >> 16);
    bytes[3] = (uint8_t)(val >> 24);
    return val_null();
}

// write_u64_be(ptr, offset, val): void - Write u64 as big-endian to pointer+offset
Value builtin_write_u64_be(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "write_u64_be() expects 3 arguments (ptr, offset, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "write_u64_be() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint64_t val = (uint64_t)value_to_int64(args[2]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 8, "write_u64_be()", ctx);
    if (!bytes) return val_null();
    bytes[0] = (uint8_t)(val >> 56);
    bytes[1] = (uint8_t)(val >> 48);
    bytes[2] = (uint8_t)(val >> 40);
    bytes[3] = (uint8_t)(val >> 32);
    bytes[4] = (uint8_t)(val >> 24);
    bytes[5] = (uint8_t)(val >> 16);
    bytes[6] = (uint8_t)(val >> 8);
    bytes[7] = (uint8_t)(val & 0xFF);
    return val_null();
}

// write_u64_le(ptr, offset, val): void - Write u64 as little-endian to pointer+offset
Value builtin_write_u64_le(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "write_u64_le() expects 3 arguments (ptr, offset, value)");
        return val_null();
    }
    if (args[0].type != VAL_PTR && args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "write_u64_le() first argument must be a ptr or buffer");
        return val_null();
    }
    int offset = value_to_int(args[1]);
    uint64_t val = (uint64_t)value_to_int64(args[2]);
    uint8_t *bytes = byteorder_addr(args[0], offset, 8, "write_u64_le()", ctx);
    if (!bytes) return val_null();
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)(val >> 8);
    bytes[2] = (uint8_t)(val >> 16);
    bytes[3] = (uint8_t)(val >> 24);
    bytes[4] = (uint8_t)(val >> 32);
    bytes[5] = (uint8_t)(val >> 40);
    bytes[6] = (uint8_t)(val >> 48);
    bytes[7] = (uint8_t)(val >> 56);
    return val_null();
}
