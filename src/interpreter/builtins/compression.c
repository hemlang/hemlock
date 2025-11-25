#include "internal.h"
#include <zlib.h>

// ============================================================================
// ZLIB COMPRESSION BUILTINS
// ============================================================================

// __zlib_compress(data: string, level: i32) -> buffer
// Compress string data using zlib deflate
Value builtin_zlib_compress(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "zlib_compress() expects 2 arguments (data, level)");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "zlib_compress() first argument must be string");
        return val_null();
    }

    if (!is_numeric(args[1])) {
        runtime_error(ctx, "zlib_compress() second argument must be number (level)");
        return val_null();
    }

    String *str = args[0].as.as_string;
    int level = value_to_int(args[1]);

    // Validate compression level
    if (level < -1 || level > 9) {
        runtime_error(ctx, "zlib_compress() level must be -1 to 9");
        return val_null();
    }

    // Handle empty input - return empty buffer
    if (str->length == 0) {
        // Create empty buffer (size 1 minimum, set length to 0)
        Value buf_val = val_buffer(1);
        Buffer *buf = buf_val.as.as_buffer;
        buf->length = 0;
        return buf_val;
    }

    // Calculate maximum compressed size
    uLong source_len = str->length;
    uLong dest_len = compressBound(source_len);

    // Allocate destination buffer
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        runtime_error(ctx, "zlib_compress() memory allocation failed");
        return val_null();
    }

    // Compress
    int result = compress2(dest, &dest_len, (const Bytef *)str->data, source_len, level);

    if (result != Z_OK) {
        free(dest);
        const char *msg;
        switch (result) {
            case Z_MEM_ERROR: msg = "memory allocation failed"; break;
            case Z_BUF_ERROR: msg = "buffer too small"; break;
            case Z_STREAM_ERROR: msg = "invalid compression level"; break;
            default: msg = "unknown error"; break;
        }
        runtime_error(ctx, "zlib_compress() failed: %s", msg);
        return val_null();
    }

    // Create buffer with compressed data
    Value buf_val = val_buffer((int)dest_len);
    Buffer *buf = buf_val.as.as_buffer;
    memcpy(buf->data, dest, dest_len);
    free(dest);

    return buf_val;
}

// __zlib_decompress(data: buffer, max_size: i64) -> string
// Decompress zlib-compressed data
Value builtin_zlib_decompress(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "zlib_decompress() expects 2 arguments (data, max_size)");
        return val_null();
    }

    if (args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "zlib_decompress() first argument must be buffer");
        return val_null();
    }

    if (!is_numeric(args[1])) {
        runtime_error(ctx, "zlib_decompress() second argument must be number (max_size)");
        return val_null();
    }

    Buffer *buf = args[0].as.as_buffer;
    size_t max_size = (size_t)value_to_int(args[1]);

    // Handle empty input
    if (buf->length == 0) {
        return val_string("");
    }

    // Allocate destination buffer
    uLong dest_len = max_size;
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        runtime_error(ctx, "zlib_decompress() memory allocation failed");
        return val_null();
    }

    // Cast buffer data to Bytef*
    const Bytef *source = (const Bytef *)buf->data;

    // Decompress
    int result = uncompress(dest, &dest_len, source, buf->length);

    if (result != Z_OK) {
        free(dest);
        const char *msg;
        switch (result) {
            case Z_MEM_ERROR: msg = "memory allocation failed"; break;
            case Z_BUF_ERROR: msg = "output buffer too small"; break;
            case Z_DATA_ERROR: msg = "corrupted or invalid data"; break;
            default: msg = "unknown error"; break;
        }
        runtime_error(ctx, "zlib_decompress() failed: %s", msg);
        return val_null();
    }

    // Create string from decompressed data - use val_string_take which takes ownership
    char *result_str = malloc(dest_len + 1);
    if (!result_str) {
        free(dest);
        runtime_error(ctx, "zlib_decompress() memory allocation failed");
        return val_null();
    }
    memcpy(result_str, dest, dest_len);
    result_str[dest_len] = '\0';
    free(dest);

    return val_string_take(result_str, (int)dest_len, (int)dest_len + 1);
}

// __gzip_compress(data: string, level: i32) -> buffer
// Compress string data using gzip format (with header and checksum)
Value builtin_gzip_compress(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "gzip_compress() expects 2 arguments (data, level)");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "gzip_compress() first argument must be string");
        return val_null();
    }

    if (!is_numeric(args[1])) {
        runtime_error(ctx, "gzip_compress() second argument must be number (level)");
        return val_null();
    }

    String *str = args[0].as.as_string;
    int level = value_to_int(args[1]);

    // Validate compression level
    if (level < -1 || level > 9) {
        runtime_error(ctx, "gzip_compress() level must be -1 to 9");
        return val_null();
    }

    // Handle empty input - gzip still produces header/trailer
    if (str->length == 0) {
        // Minimum valid gzip for empty content
        unsigned char empty_gzip[] = {
            0x1f, 0x8b, 0x08, 0x00,  // Magic + CM + FLG
            0x00, 0x00, 0x00, 0x00,  // MTIME
            0x00, 0xff,              // XFL + OS
            0x03, 0x00,              // Empty deflate block
            0x00, 0x00, 0x00, 0x00,  // CRC32
            0x00, 0x00, 0x00, 0x00   // ISIZE
        };
        Value buf_val = val_buffer(sizeof(empty_gzip));
        Buffer *buf = buf_val.as.as_buffer;
        memcpy(buf->data, empty_gzip, sizeof(empty_gzip));
        return buf_val;
    }

    // Initialize z_stream for gzip compression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // windowBits = 15 + 16 = 31 for gzip format
    int ret = deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        runtime_error(ctx, "gzip_compress() initialization failed");
        return val_null();
    }

    // Calculate output buffer size (worst case + gzip overhead)
    uLong dest_len = compressBound(str->length) + 18;
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        deflateEnd(&strm);
        runtime_error(ctx, "gzip_compress() memory allocation failed");
        return val_null();
    }

    // Set input/output
    strm.next_in = (Bytef *)str->data;
    strm.avail_in = str->length;
    strm.next_out = dest;
    strm.avail_out = dest_len;

    // Compress all at once
    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        free(dest);
        deflateEnd(&strm);
        runtime_error(ctx, "gzip_compress() compression failed");
        return val_null();
    }

    // Get actual output size
    size_t output_len = strm.total_out;
    deflateEnd(&strm);

    // Create buffer with compressed data
    Value buf_val = val_buffer((int)output_len);
    Buffer *buf = buf_val.as.as_buffer;
    memcpy(buf->data, dest, output_len);
    free(dest);

    return buf_val;
}

// __gzip_decompress(data: buffer, max_size: i64) -> string
// Decompress gzip-compressed data
Value builtin_gzip_decompress(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "gzip_decompress() expects 2 arguments (data, max_size)");
        return val_null();
    }

    if (args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "gzip_decompress() first argument must be buffer");
        return val_null();
    }

    if (!is_numeric(args[1])) {
        runtime_error(ctx, "gzip_decompress() second argument must be number (max_size)");
        return val_null();
    }

    Buffer *buf = args[0].as.as_buffer;
    size_t max_size = (size_t)value_to_int(args[1]);

    // Handle empty input
    if (buf->length == 0) {
        runtime_error(ctx, "gzip_decompress() requires non-empty input");
        return val_null();
    }

    // Verify gzip magic bytes - cast data to unsigned char*
    unsigned char *data = (unsigned char *)buf->data;
    if (buf->length < 10 || data[0] != 0x1f || data[1] != 0x8b) {
        runtime_error(ctx, "gzip_decompress() invalid gzip data (bad magic bytes)");
        return val_null();
    }

    // Initialize z_stream for gzip decompression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // windowBits = 15 + 16 = 31 for gzip format
    int ret = inflateInit2(&strm, 15 + 16);
    if (ret != Z_OK) {
        runtime_error(ctx, "gzip_decompress() initialization failed");
        return val_null();
    }

    // Allocate destination buffer
    Bytef *dest = malloc(max_size);
    if (!dest) {
        inflateEnd(&strm);
        runtime_error(ctx, "gzip_decompress() memory allocation failed");
        return val_null();
    }

    // Set input/output
    strm.next_in = (Bytef *)buf->data;
    strm.avail_in = buf->length;
    strm.next_out = dest;
    strm.avail_out = max_size;

    // Decompress
    ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        free(dest);
        inflateEnd(&strm);
        const char *msg;
        switch (ret) {
            case Z_MEM_ERROR: msg = "memory allocation failed"; break;
            case Z_BUF_ERROR: msg = "output buffer too small"; break;
            case Z_DATA_ERROR: msg = "corrupted or invalid gzip data"; break;
            default: msg = "unknown error"; break;
        }
        runtime_error(ctx, "gzip_decompress() failed: %s", msg);
        return val_null();
    }

    // Get actual output size
    size_t output_len = strm.total_out;
    inflateEnd(&strm);

    // Create string from decompressed data
    char *result_str = malloc(output_len + 1);
    if (!result_str) {
        free(dest);
        runtime_error(ctx, "gzip_decompress() memory allocation failed");
        return val_null();
    }
    memcpy(result_str, dest, output_len);
    result_str[output_len] = '\0';
    free(dest);

    return val_string_take(result_str, (int)output_len, (int)output_len + 1);
}

// __zlib_compress_bound(source_len: i64) -> i64
// Calculate maximum compressed size for a given input size
Value builtin_zlib_compress_bound(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "zlib_compress_bound() expects 1 argument (source_len)");
        return val_null();
    }

    if (!is_numeric(args[0])) {
        runtime_error(ctx, "zlib_compress_bound() argument must be number");
        return val_null();
    }

    uLong source_len = (uLong)value_to_int(args[0]);
    uLong bound = compressBound(source_len);

    return val_i64((int64_t)bound);
}

// __crc32(data: buffer) -> u32
// Calculate CRC32 checksum of buffer data
Value builtin_crc32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "crc32() expects 1 argument (data)");
        return val_null();
    }

    if (args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "crc32() argument must be buffer");
        return val_null();
    }

    Buffer *buf = args[0].as.as_buffer;

    // Calculate CRC32
    uLong crc_val = crc32(0L, Z_NULL, 0);
    crc_val = crc32(crc_val, (const Bytef *)buf->data, buf->length);

    return val_u32((uint32_t)crc_val);
}

// __adler32(data: buffer) -> u32
// Calculate Adler-32 checksum of buffer data
Value builtin_adler32(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "adler32() expects 1 argument (data)");
        return val_null();
    }

    if (args[0].type != VAL_BUFFER) {
        runtime_error(ctx, "adler32() argument must be buffer");
        return val_null();
    }

    Buffer *buf = args[0].as.as_buffer;

    // Calculate Adler-32
    uLong adler_val = adler32(0L, Z_NULL, 0);
    adler_val = adler32(adler_val, (const Bytef *)buf->data, buf->length);

    return val_u32((uint32_t)adler_val);
}
