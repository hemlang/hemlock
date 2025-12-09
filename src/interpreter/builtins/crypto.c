#include "internal.h"
#include <openssl/sha.h>
#include <openssl/md5.h>

// ============================================================================
// CRYPTOGRAPHIC HASH BUILTINS (OpenSSL)
// ============================================================================

// Helper: Convert bytes to hexadecimal string
static Value bytes_to_hex_string(const unsigned char *bytes, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    char *hex = malloc(len * 2 + 1);
    if (!hex) return val_null();

    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';

    Value result = val_string_take(hex, len * 2, len * 2 + 1);
    return result;
}

// __sha256(input: string) -> string
// Compute SHA-256 hash, returns hex string
Value builtin_sha256(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "__sha256() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "__sha256() argument must be string");
        return val_null();
    }

    String *str = args[0].as.as_string;
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256((const unsigned char *)str->data, str->length, hash);

    return bytes_to_hex_string(hash, SHA256_DIGEST_LENGTH);
}

// __sha512(input: string) -> string
// Compute SHA-512 hash, returns hex string
Value builtin_sha512(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "__sha512() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "__sha512() argument must be string");
        return val_null();
    }

    String *str = args[0].as.as_string;
    unsigned char hash[SHA512_DIGEST_LENGTH];

    SHA512((const unsigned char *)str->data, str->length, hash);

    return bytes_to_hex_string(hash, SHA512_DIGEST_LENGTH);
}

// __md5(input: string) -> string
// Compute MD5 hash, returns hex string
// WARNING: MD5 is cryptographically broken, use only for legacy compatibility
Value builtin_md5(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "__md5() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "__md5() argument must be string");
        return val_null();
    }

    String *str = args[0].as.as_string;
    unsigned char hash[MD5_DIGEST_LENGTH];

    MD5((const unsigned char *)str->data, str->length, hash);

    return bytes_to_hex_string(hash, MD5_DIGEST_LENGTH);
}
