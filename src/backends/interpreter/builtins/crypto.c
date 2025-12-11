#include "internal.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#include <openssl/err.h>

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
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) return val_null();

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1 ||
        EVP_DigestUpdate(mdctx, str->data, str->length) != 1 ||
        EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return val_null();
    }
    EVP_MD_CTX_free(mdctx);

    return bytes_to_hex_string(hash, hash_len);
}

// ============================================================================
// ECDSA KEY GENERATION (OpenSSL 3.0+)
// ============================================================================

// __ecdsa_generate_key(curve?: string) -> { private_key: ptr, public_key: ptr }
// Generate an ECDSA key pair using the specified curve (default: P-256/prime256v1)
// Returns an object with private_key and public_key pointers (both point to same EVP_PKEY)
Value builtin_ecdsa_generate_key(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;

    // Default curve is P-256 (prime256v1)
    const char *curve_name = "prime256v1";

    // Optional curve name argument
    if (num_args >= 1 && args[0].type == VAL_STRING) {
        curve_name = args[0].as.as_string->data;
    }

    // Use EVP_EC_gen which is the OpenSSL 3.0+ way to generate EC keys
    // This is NOT variadic, unlike EVP_PKEY_Q_keygen
    EVP_PKEY *pkey = EVP_EC_gen(curve_name);

    if (!pkey) {
        // Clear OpenSSL error queue and return null
        ERR_clear_error();
        runtime_error(ctx, "__ecdsa_generate_key() failed to generate key for curve: %s", curve_name);
        return val_null();
    }

    // Create result object with private_key and public_key
    // Both point to the same EVP_PKEY* (the key contains both private and public parts)
    Object *obj = object_new(NULL, 2);
    obj->field_names[0] = strdup("private_key");
    obj->field_values[0] = val_ptr(pkey);
    obj->num_fields++;
    obj->field_names[1] = strdup("public_key");
    obj->field_values[1] = val_ptr(pkey);
    obj->num_fields++;

    return val_object(obj);
}

// Helper: Get field value from object by name
static Value object_get_field(Object *obj, const char *name) {
    int idx = object_lookup_field(obj, name);
    if (idx < 0) {
        return val_null();
    }
    return obj->field_values[idx];
}

// __ecdsa_free_key(keypair) -> null
// Free an ECDSA key pair
Value builtin_ecdsa_free_key(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "__ecdsa_free_key() expects 1 argument");
        return val_null();
    }

    if (args[0].type != VAL_OBJECT) {
        runtime_error(ctx, "__ecdsa_free_key() argument must be an object");
        return val_null();
    }

    Object *obj = args[0].as.as_object;
    Value pk_val = object_get_field(obj, "private_key");

    if (pk_val.type == VAL_PTR && pk_val.as.as_ptr != NULL) {
        EVP_PKEY_free((EVP_PKEY *)pk_val.as.as_ptr);
    }

    return val_null();
}

// __ecdsa_sign(data: string, keypair: object) -> buffer
// Sign data with ECDSA private key using SHA-256
Value builtin_ecdsa_sign(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 2) {
        runtime_error(ctx, "__ecdsa_sign() expects 2 arguments (data, keypair)");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "__ecdsa_sign() first argument must be string");
        return val_null();
    }

    if (args[1].type != VAL_OBJECT) {
        runtime_error(ctx, "__ecdsa_sign() second argument must be keypair object");
        return val_null();
    }

    String *data = args[0].as.as_string;
    Object *keypair = args[1].as.as_object;
    Value pk_val = object_get_field(keypair, "private_key");

    if (pk_val.type != VAL_PTR || pk_val.as.as_ptr == NULL) {
        runtime_error(ctx, "__ecdsa_sign() keypair must have valid private_key");
        return val_null();
    }

    EVP_PKEY *pkey = (EVP_PKEY *)pk_val.as.as_ptr;

    // Create signing context
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        runtime_error(ctx, "__ecdsa_sign() failed to create MD context");
        return val_null();
    }

    // Initialize signing with SHA-256
    if (EVP_DigestSignInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        runtime_error(ctx, "__ecdsa_sign() DigestSignInit failed");
        return val_null();
    }

    // Determine signature length
    size_t sig_len = 0;
    if (EVP_DigestSign(md_ctx, NULL, &sig_len, (const unsigned char *)data->data, data->length) != 1) {
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        runtime_error(ctx, "__ecdsa_sign() failed to get signature length");
        return val_null();
    }

    // Allocate signature buffer
    unsigned char *sig = malloc(sig_len);
    if (!sig) {
        EVP_MD_CTX_free(md_ctx);
        runtime_error(ctx, "__ecdsa_sign() memory allocation failed");
        return val_null();
    }

    // Sign the data
    if (EVP_DigestSign(md_ctx, sig, &sig_len, (const unsigned char *)data->data, data->length) != 1) {
        free(sig);
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        runtime_error(ctx, "__ecdsa_sign() signing failed");
        return val_null();
    }

    EVP_MD_CTX_free(md_ctx);

    // Create buffer with signature
    Value buf_val = val_buffer((int)sig_len);
    Buffer *buf = buf_val.as.as_buffer;
    memcpy(buf->data, sig, sig_len);
    free(sig);

    return buf_val;
}

// __ecdsa_verify(data: string, signature: buffer, keypair: object) -> bool
// Verify ECDSA signature
Value builtin_ecdsa_verify(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 3) {
        runtime_error(ctx, "__ecdsa_verify() expects 3 arguments (data, signature, keypair)");
        return val_null();
    }

    if (args[0].type != VAL_STRING) {
        runtime_error(ctx, "__ecdsa_verify() first argument must be string");
        return val_null();
    }

    if (args[1].type != VAL_BUFFER) {
        runtime_error(ctx, "__ecdsa_verify() second argument must be buffer");
        return val_null();
    }

    if (args[2].type != VAL_OBJECT) {
        runtime_error(ctx, "__ecdsa_verify() third argument must be keypair object");
        return val_null();
    }

    String *data = args[0].as.as_string;
    Buffer *sig_buf = args[1].as.as_buffer;
    Object *keypair = args[2].as.as_object;
    Value pk_val = object_get_field(keypair, "public_key");

    if (pk_val.type != VAL_PTR || pk_val.as.as_ptr == NULL) {
        runtime_error(ctx, "__ecdsa_verify() keypair must have valid public_key");
        return val_null();
    }

    EVP_PKEY *pkey = (EVP_PKEY *)pk_val.as.as_ptr;

    // Create verification context
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        runtime_error(ctx, "__ecdsa_verify() failed to create MD context");
        return val_null();
    }

    // Initialize verification with SHA-256
    if (EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        runtime_error(ctx, "__ecdsa_verify() DigestVerifyInit failed");
        return val_null();
    }

    // Verify the signature
    int result = EVP_DigestVerify(md_ctx, sig_buf->data, sig_buf->length,
                                   (const unsigned char *)data->data, data->length);

    EVP_MD_CTX_free(md_ctx);
    ERR_clear_error();

    // result == 1 means valid, 0 means invalid, < 0 means error
    return val_bool(result == 1);
}
