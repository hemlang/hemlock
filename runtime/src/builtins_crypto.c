/*
 * Hemlock Runtime Library - Crypto and Compression Operations
 *
 * Cryptographic functions (SHA, MD5, ECDSA) and compression (zlib, gzip).
 */

#include "builtins_internal.h"
#include <stdatomic.h>

// ========== COMPRESSION OPERATIONS ==========

#ifdef HML_HAVE_ZLIB

// zlib_compress(data: string, level: i32) -> buffer
HmlValue hml_zlib_compress(HmlValue data, HmlValue level_val) {
    if (data.type != HML_VAL_STRING || !data.as.as_string) {
        hml_runtime_error("zlib_compress() first argument must be string");
    }

    int level = (int)level_val.as.as_i32;
    if (level < -1 || level > 9) {
        hml_runtime_error("zlib_compress() level must be -1 to 9");
    }

    HmlString *str = data.as.as_string;

    // Handle empty input
    if (str->length == 0) {
        HmlValue buf = hml_val_buffer(1);
        buf.as.as_buffer->length = 0;
        return buf;
    }

    // Calculate maximum compressed size
    uLong source_len = str->length;
    uLong dest_len = compressBound(source_len);

    // Allocate destination buffer
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        hml_runtime_error("zlib_compress() memory allocation failed");
    }

    // Compress
    int result = compress2(dest, &dest_len, (const Bytef *)str->data, source_len, level);

    if (result != Z_OK) {
        free(dest);
        hml_runtime_error("zlib_compress() compression failed");
    }

    // Create buffer with compressed data
    HmlValue buf = hml_val_buffer((int32_t)dest_len);
    memcpy(buf.as.as_buffer->data, dest, dest_len);
    free(dest);

    return buf;
}

// zlib_decompress(data: buffer, max_size: i64) -> string
HmlValue hml_zlib_decompress(HmlValue data, HmlValue max_size_val) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("zlib_decompress() first argument must be buffer");
    }

    size_t max_size = (size_t)max_size_val.as.as_i64;
    HmlBuffer *buf = data.as.as_buffer;

    // Handle empty input
    if (buf->length == 0) {
        return hml_val_string("");
    }

    // Allocate destination buffer
    uLong dest_len = max_size;
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        hml_runtime_error("zlib_decompress() memory allocation failed");
    }

    // Decompress
    int result = uncompress(dest, &dest_len, (const Bytef *)buf->data, buf->length);

    if (result != Z_OK) {
        free(dest);
        hml_runtime_error("zlib_decompress() decompression failed");
    }

    // Create string from decompressed data
    char *result_str = malloc(dest_len + 1);
    if (!result_str) {
        free(dest);
        hml_runtime_error("zlib_decompress() memory allocation failed");
    }
    memcpy(result_str, dest, dest_len);
    result_str[dest_len] = '\0';
    free(dest);

    HmlValue ret = hml_val_string(result_str);
    free(result_str);
    return ret;
}

// gzip_compress(data: string, level: i32) -> buffer
HmlValue hml_gzip_compress(HmlValue data, HmlValue level_val) {
    if (data.type != HML_VAL_STRING || !data.as.as_string) {
        hml_runtime_error("gzip_compress() first argument must be string");
    }

    int level = (int)level_val.as.as_i32;
    if (level < -1 || level > 9) {
        hml_runtime_error("gzip_compress() level must be -1 to 9");
    }

    HmlString *str = data.as.as_string;

    // Handle empty input - gzip still produces header/trailer
    if (str->length == 0) {
        unsigned char empty_gzip[] = {
            0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        HmlValue buf = hml_val_buffer(sizeof(empty_gzip));
        memcpy(buf.as.as_buffer->data, empty_gzip, sizeof(empty_gzip));
        return buf;
    }

    // Initialize z_stream for gzip compression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // windowBits = 15 + 16 = 31 for gzip format
    int ret = deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        hml_runtime_error("gzip_compress() initialization failed");
    }

    // Calculate output buffer size
    uLong dest_len = compressBound(str->length) + 18;
    Bytef *dest = malloc(dest_len);
    if (!dest) {
        deflateEnd(&strm);
        hml_runtime_error("gzip_compress() memory allocation failed");
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
        hml_runtime_error("gzip_compress() compression failed");
    }

    size_t output_len = strm.total_out;
    deflateEnd(&strm);

    HmlValue buf = hml_val_buffer((int32_t)output_len);
    memcpy(buf.as.as_buffer->data, dest, output_len);
    free(dest);

    return buf;
}

// gzip_decompress(data: buffer, max_size: i64) -> string
HmlValue hml_gzip_decompress(HmlValue data, HmlValue max_size_val) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("gzip_decompress() first argument must be buffer");
    }

    size_t max_size = (size_t)max_size_val.as.as_i64;
    HmlBuffer *buf = data.as.as_buffer;

    // Handle empty input
    if (buf->length == 0) {
        hml_runtime_error("gzip_decompress() requires non-empty input");
    }

    // Verify gzip magic bytes
    unsigned char *buf_data = (unsigned char *)buf->data;
    if (buf->length < 10 || buf_data[0] != 0x1f || buf_data[1] != 0x8b) {
        hml_runtime_error("gzip_decompress() invalid gzip data");
    }

    // Initialize z_stream for gzip decompression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    // windowBits = 15 + 16 = 31 for gzip format
    int ret = inflateInit2(&strm, 15 + 16);
    if (ret != Z_OK) {
        hml_runtime_error("gzip_decompress() initialization failed");
    }

    // Allocate destination buffer
    Bytef *dest = malloc(max_size);
    if (!dest) {
        inflateEnd(&strm);
        hml_runtime_error("gzip_decompress() memory allocation failed");
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
        hml_runtime_error("gzip_decompress() decompression failed");
    }

    size_t output_len = strm.total_out;
    inflateEnd(&strm);

    // Create string from decompressed data
    char *result_str = malloc(output_len + 1);
    if (!result_str) {
        free(dest);
        hml_runtime_error("gzip_decompress() memory allocation failed");
    }
    memcpy(result_str, dest, output_len);
    result_str[output_len] = '\0';
    free(dest);

    HmlValue result = hml_val_string(result_str);
    free(result_str);
    return result;
}

// zlib_compress_bound(source_len: i64) -> i64
HmlValue hml_zlib_compress_bound(HmlValue source_len_val) {
    uLong source_len = (uLong)source_len_val.as.as_i64;
    uLong bound = compressBound(source_len);
    return hml_val_i64((int64_t)bound);
}

// crc32(data: buffer) -> u32
HmlValue hml_crc32_val(HmlValue data) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("crc32() argument must be buffer");
    }

    HmlBuffer *buf = data.as.as_buffer;
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)buf->data, buf->length);

    return hml_val_u32((uint32_t)crc);
}

// adler32(data: buffer) -> u32
HmlValue hml_adler32_val(HmlValue data) {
    if (data.type != HML_VAL_BUFFER || !data.as.as_buffer) {
        hml_runtime_error("adler32() argument must be buffer");
    }

    HmlBuffer *buf = data.as.as_buffer;
    uLong adler = adler32(0L, Z_NULL, 0);
    adler = adler32(adler, (const Bytef *)buf->data, buf->length);

    return hml_val_u32((uint32_t)adler);
}

// Compression builtin wrappers
HmlValue hml_builtin_zlib_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_zlib_compress(data, level);
}

HmlValue hml_builtin_zlib_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_zlib_decompress(data, max_size);
}

HmlValue hml_builtin_gzip_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_gzip_compress(data, level);
}

HmlValue hml_builtin_gzip_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_gzip_decompress(data, max_size);
}

HmlValue hml_builtin_zlib_compress_bound(HmlClosureEnv *env, HmlValue source_len) {
    (void)env;
    return hml_zlib_compress_bound(source_len);
}

HmlValue hml_builtin_crc32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_crc32_val(data);
}

HmlValue hml_builtin_adler32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_adler32_val(data);
}

#else /* !HML_HAVE_ZLIB */

// Stub implementations when zlib is not available
HmlValue hml_zlib_compress(HmlValue data, HmlValue level_val) {
    (void)data; (void)level_val;
    hml_runtime_error("zlib_compress() not available - zlib not installed");
}

HmlValue hml_zlib_decompress(HmlValue data, HmlValue max_size_val) {
    (void)data; (void)max_size_val;
    hml_runtime_error("zlib_decompress() not available - zlib not installed");
}

HmlValue hml_gzip_compress(HmlValue data, HmlValue level_val) {
    (void)data; (void)level_val;
    hml_runtime_error("gzip_compress() not available - zlib not installed");
}

HmlValue hml_gzip_decompress(HmlValue data, HmlValue max_size_val) {
    (void)data; (void)max_size_val;
    hml_runtime_error("gzip_decompress() not available - zlib not installed");
}

HmlValue hml_zlib_compress_bound(HmlValue source_len_val) {
    (void)source_len_val;
    hml_runtime_error("zlib_compress_bound() not available - zlib not installed");
}

HmlValue hml_crc32_val(HmlValue data) {
    (void)data;
    hml_runtime_error("crc32() not available - zlib not installed");
}

HmlValue hml_adler32_val(HmlValue data) {
    (void)data;
    hml_runtime_error("adler32() not available - zlib not installed");
}

HmlValue hml_builtin_zlib_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_zlib_compress(data, level);
}

HmlValue hml_builtin_zlib_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_zlib_decompress(data, max_size);
}

HmlValue hml_builtin_gzip_compress(HmlClosureEnv *env, HmlValue data, HmlValue level) {
    (void)env;
    return hml_gzip_compress(data, level);
}

HmlValue hml_builtin_gzip_decompress(HmlClosureEnv *env, HmlValue data, HmlValue max_size) {
    (void)env;
    return hml_gzip_decompress(data, max_size);
}

HmlValue hml_builtin_zlib_compress_bound(HmlClosureEnv *env, HmlValue source_len) {
    (void)env;
    return hml_zlib_compress_bound(source_len);
}

HmlValue hml_builtin_crc32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_crc32_val(data);
}

HmlValue hml_builtin_adler32(HmlClosureEnv *env, HmlValue data) {
    (void)env;
    return hml_adler32_val(data);
}

#endif /* HML_HAVE_ZLIB */

// ========== CRYPTOGRAPHIC HASH FUNCTIONS (OpenSSL) ==========

// Helper: Convert bytes to hexadecimal string
static HmlValue bytes_to_hex_string(const unsigned char *bytes, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    char *hex = malloc(len * 2 + 1);
    if (!hex) return hml_val_string("");

    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';

    HmlValue result = hml_val_string(hex);
    free(hex);
    return result;
}

// SHA-256 hash - returns hex string
HmlValue hml_hash_sha256(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("sha256() requires string argument");
    }

    const char *data = input.as.as_string->data;
    size_t len = input.as.as_string->length;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)data, len, hash);

    return bytes_to_hex_string(hash, SHA256_DIGEST_LENGTH);
}

// SHA-512 hash - returns hex string
HmlValue hml_hash_sha512(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("sha512() requires string argument");
    }

    const char *data = input.as.as_string->data;
    size_t len = input.as.as_string->length;

    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512((const unsigned char *)data, len, hash);

    return bytes_to_hex_string(hash, SHA512_DIGEST_LENGTH);
}

// MD5 hash - returns hex string
HmlValue hml_hash_md5(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("md5() requires string argument");
    }

    const char *data = input.as.as_string->data;
    size_t len = input.as.as_string->length;

    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5((const unsigned char *)data, len, hash);

    return bytes_to_hex_string(hash, MD5_DIGEST_LENGTH);
}

// Builtin wrappers for function-as-value usage
HmlValue hml_builtin_hash_sha256(HmlClosureEnv *env, HmlValue input) {
    (void)env;
    return hml_hash_sha256(input);
}

HmlValue hml_builtin_hash_sha512(HmlClosureEnv *env, HmlValue input) {
    (void)env;
    return hml_hash_sha512(input);
}

HmlValue hml_builtin_hash_md5(HmlClosureEnv *env, HmlValue input) {
    (void)env;
    return hml_hash_md5(input);
}

// ========== ECDSA OPERATIONS ==========

// Helper: Create an object with keypair fields
static HmlValue create_keypair_object(void *pkey) {
    HmlObject *obj = malloc(sizeof(HmlObject));
    obj->type_name = NULL;
    obj->capacity = 2;
    obj->field_names = malloc(sizeof(char*) * 2);
    obj->field_values = malloc(sizeof(HmlValue) * 2);
    obj->num_fields = 0;
    obj->ref_count = 1;
    atomic_store(&obj->freed, 0);

    obj->field_names[0] = strdup("private_key");
    obj->field_values[0] = hml_val_ptr(pkey);
    obj->num_fields++;
    obj->field_names[1] = strdup("public_key");
    obj->field_values[1] = hml_val_ptr(pkey);
    obj->num_fields++;

    HmlValue result;
    result.type = HML_VAL_OBJECT;
    result.as.as_object = obj;
    return result;
}

// Helper: Get field value from object
static HmlValue object_get_field_rt(HmlObject *obj, const char *name) {
    for (int i = 0; i < obj->num_fields; i++) {
        if (obj->field_names[i] && strcmp(obj->field_names[i], name) == 0) {
            return obj->field_values[i];
        }
    }
    return hml_val_null();
}

// Generate ECDSA key pair
HmlValue hml_ecdsa_generate_key(HmlValue curve_arg) {
    const char *curve_name = "prime256v1";  // Default P-256

    if (curve_arg.type == HML_VAL_STRING) {
        HmlString *s = curve_arg.as.as_string;
        curve_name = s->data;
    }

    // Use EVP_EC_gen (OpenSSL 3.0+ non-variadic API)
    EVP_PKEY *pkey = EVP_EC_gen(curve_name);

    if (!pkey) {
        ERR_clear_error();
        hml_runtime_error("__ecdsa_generate_key() failed for curve: %s", curve_name);
    }

    return create_keypair_object(pkey);
}

// Free ECDSA key pair
HmlValue hml_ecdsa_free_key(HmlValue keypair) {
    if (keypair.type != HML_VAL_OBJECT) {
        hml_runtime_error("__ecdsa_free_key() requires object argument");
    }

    HmlObject *obj = keypair.as.as_object;
    HmlValue pk_val = object_get_field_rt(obj, "private_key");

    if (pk_val.type == HML_VAL_PTR && pk_val.as.as_ptr != NULL) {
        EVP_PKEY_free((EVP_PKEY *)pk_val.as.as_ptr);
    }

    return hml_val_null();
}

// Sign data with ECDSA
HmlValue hml_ecdsa_sign(HmlValue data_val, HmlValue keypair) {
    if (data_val.type != HML_VAL_STRING) {
        hml_runtime_error("__ecdsa_sign() first argument must be string");
    }
    if (keypair.type != HML_VAL_OBJECT) {
        hml_runtime_error("__ecdsa_sign() second argument must be keypair object");
    }

    HmlString *data = data_val.as.as_string;
    HmlObject *obj = keypair.as.as_object;
    HmlValue pk_val = object_get_field_rt(obj, "private_key");

    if (pk_val.type != HML_VAL_PTR || pk_val.as.as_ptr == NULL) {
        hml_runtime_error("__ecdsa_sign() keypair must have valid private_key");
    }

    EVP_PKEY *pkey = (EVP_PKEY *)pk_val.as.as_ptr;

    // Create signing context
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        hml_runtime_error("__ecdsa_sign() failed to create MD context");
    }

    // Initialize signing with SHA-256
    if (EVP_DigestSignInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        hml_runtime_error("__ecdsa_sign() DigestSignInit failed");
    }

    // Determine signature length
    size_t sig_len = 0;
    if (EVP_DigestSign(md_ctx, NULL, &sig_len, (const unsigned char *)data->data, data->length) != 1) {
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        hml_runtime_error("__ecdsa_sign() failed to get signature length");
    }

    // Allocate signature buffer
    unsigned char *sig = malloc(sig_len);
    if (!sig) {
        EVP_MD_CTX_free(md_ctx);
        hml_runtime_error("__ecdsa_sign() memory allocation failed");
    }

    // Sign the data
    if (EVP_DigestSign(md_ctx, sig, &sig_len, (const unsigned char *)data->data, data->length) != 1) {
        free(sig);
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        hml_runtime_error("__ecdsa_sign() signing failed");
    }

    EVP_MD_CTX_free(md_ctx);

    // Create buffer with signature
    HmlValue buf_val = hml_val_buffer((int)sig_len);
    HmlBuffer *buf = buf_val.as.as_buffer;
    memcpy(buf->data, sig, sig_len);
    free(sig);

    return buf_val;
}

// Verify ECDSA signature
HmlValue hml_ecdsa_verify(HmlValue data_val, HmlValue sig_val, HmlValue keypair) {
    if (data_val.type != HML_VAL_STRING) {
        hml_runtime_error("__ecdsa_verify() first argument must be string");
    }
    if (sig_val.type != HML_VAL_BUFFER) {
        hml_runtime_error("__ecdsa_verify() second argument must be buffer");
    }
    if (keypair.type != HML_VAL_OBJECT) {
        hml_runtime_error("__ecdsa_verify() third argument must be keypair object");
    }

    HmlString *data = data_val.as.as_string;
    HmlBuffer *sig_buf = sig_val.as.as_buffer;
    HmlObject *obj = keypair.as.as_object;
    HmlValue pk_val = object_get_field_rt(obj, "public_key");

    if (pk_val.type != HML_VAL_PTR || pk_val.as.as_ptr == NULL) {
        hml_runtime_error("__ecdsa_verify() keypair must have valid public_key");
    }

    EVP_PKEY *pkey = (EVP_PKEY *)pk_val.as.as_ptr;

    // Create verification context
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        hml_runtime_error("__ecdsa_verify() failed to create MD context");
    }

    // Initialize verification with SHA-256
    if (EVP_DigestVerifyInit(md_ctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        EVP_MD_CTX_free(md_ctx);
        ERR_clear_error();
        hml_runtime_error("__ecdsa_verify() DigestVerifyInit failed");
    }

    // Verify the signature
    int result = EVP_DigestVerify(md_ctx, sig_buf->data, sig_buf->length,
                                   (const unsigned char *)data->data, data->length);

    EVP_MD_CTX_free(md_ctx);
    ERR_clear_error();

    // result == 1 means valid, 0 means invalid, < 0 means error
    return hml_val_bool(result == 1);
}

// Builtin wrappers for function-as-value usage
HmlValue hml_builtin_ecdsa_generate_key(HmlClosureEnv *env, HmlValue curve) {
    (void)env;
    return hml_ecdsa_generate_key(curve);
}

HmlValue hml_builtin_ecdsa_free_key(HmlClosureEnv *env, HmlValue keypair) {
    (void)env;
    return hml_ecdsa_free_key(keypair);
}

HmlValue hml_builtin_ecdsa_sign(HmlClosureEnv *env, HmlValue data, HmlValue keypair) {
    (void)env;
    return hml_ecdsa_sign(data, keypair);
}

HmlValue hml_builtin_ecdsa_verify(HmlClosureEnv *env, HmlValue data, HmlValue sig, HmlValue keypair) {
    (void)env;
    return hml_ecdsa_verify(data, sig, keypair);
}
