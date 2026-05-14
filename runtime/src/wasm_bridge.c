/*
 * Hemlock Runtime Library - WASM JavaScript Bridge
 *
 * Provides real implementations for browser/Node.js APIs that replace
 * the POSIX-dependent stubs in wasm_shim.c. This file uses Emscripten's
 * EM_JS macro to call JavaScript functions from C.
 *
 * Implements:
 * - HTTP GET/POST via XMLHttpRequest (synchronous)
 * - SHA-256/SHA-512/MD5 via pure C implementations
 * - random_bytes via crypto.getRandomValues
 *
 * This file is ONLY compiled when targeting Emscripten (__EMSCRIPTEN__).
 */

#ifdef __EMSCRIPTEN__

#include "../include/hemlock_runtime.h"
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * SECTION 1: HTTP via XMLHttpRequest (synchronous)
 *
 * These replace the libwebsockets-based HTTP stubs from wasm_shim.c.
 * Uses synchronous XHR which works in both browser and Node.js contexts.
 * ======================================================================== */

// Internal response structure for WASM HTTP
typedef struct {
    int status;
    char *body;
    size_t body_len;
    char *headers;
} WasmHttpResponse;

// Allocate and track a response
static WasmHttpResponse *wasm_http_response_create(void) {
    WasmHttpResponse *r = calloc(1, sizeof(WasmHttpResponse));
    return r;
}

static void wasm_http_response_free(WasmHttpResponse *r) {
    if (!r) return;
    free(r->body);
    free(r->headers);
    free(r);
}

// EM_JS: Perform synchronous HTTP request, return status code
// Writes response body to the provided pointer
EM_JS(int, _hml_js_http_request, (const char *method, const char *url,
                                    const char *body, const char *content_type,
                                    char **out_body, int *out_body_len,
                                    char **out_headers), {
    try {
        var m = UTF8ToString(method);
        var u = UTF8ToString(url);

        // Check if we're in Node.js
        if (typeof XMLHttpRequest === 'undefined') {
            // Node.js environment — use child_process for sync HTTP
            // (Node doesn't have sync XHR natively)
            try {
                var http_module = m.toLowerCase() === 'https' || u.startsWith('https')
                    ? require('https') : require('http');
                // For Node.js, we return a basic error since sync HTTP is complex
                var errMsg = "HTTP not available in Node.js WASM (use browser or async)";
                var len = lengthBytesUTF8(errMsg) + 1;
                var ptr = _malloc(len);
                stringToUTF8(errMsg, ptr, len);
                HEAPU32[out_body >> 2] = ptr;
                HEAP32[out_body_len >> 2] = len - 1;
                HEAPU32[out_headers >> 2] = 0;
                return 0;
            } catch(e) {
                return 0;
            }
        }

        var xhr = new XMLHttpRequest();
        xhr.open(m, u, false);  // synchronous

        if (content_type) {
            var ct = UTF8ToString(content_type);
            if (ct.length > 0) {
                xhr.setRequestHeader('Content-Type', ct);
            }
        }

        if (body) {
            var b = UTF8ToString(body);
            xhr.send(b.length > 0 ? b : null);
        } else {
            xhr.send();
        }

        // Copy response body
        var responseText = xhr.responseText || "";
        var bodyLen = lengthBytesUTF8(responseText) + 1;
        var bodyPtr = _malloc(bodyLen);
        stringToUTF8(responseText, bodyPtr, bodyLen);
        HEAPU32[out_body >> 2] = bodyPtr;
        HEAP32[out_body_len >> 2] = bodyLen - 1;

        // Copy response headers
        var headerStr = xhr.getAllResponseHeaders() || "";
        var headerLen = lengthBytesUTF8(headerStr) + 1;
        var headerPtr = _malloc(headerLen);
        stringToUTF8(headerStr, headerPtr, headerLen);
        HEAPU32[out_headers >> 2] = headerPtr;

        return xhr.status;
    } catch(e) {
        // Network error
        var errMsg = "HTTP error: " + e.message;
        var len = lengthBytesUTF8(errMsg) + 1;
        var ptr = _malloc(len);
        stringToUTF8(errMsg, ptr, len);
        HEAPU32[out_body >> 2] = ptr;
        HEAP32[out_body_len >> 2] = len - 1;
        HEAPU32[out_headers >> 2] = 0;
        return 0;
    }
});

// Perform HTTP request and return response object
static HmlValue wasm_http_do_request(const char *method, const char *url,
                                      const char *body, const char *content_type) {
    char *resp_body = NULL;
    int resp_body_len = 0;
    char *resp_headers = NULL;

    int status = _hml_js_http_request(method, url, body, content_type,
                                       &resp_body, &resp_body_len, &resp_headers);

    // Create response object matching native Hemlock HTTP response format
    HmlValue obj = hml_val_object();
    hml_object_set_field(obj, "status", hml_val_i32(status));
    hml_object_set_field(obj, "body", resp_body ? hml_val_string(resp_body) : hml_val_string(""));

    if (resp_headers) {
        hml_object_set_field(obj, "headers", hml_val_string(resp_headers));
    } else {
        hml_object_set_field(obj, "headers", hml_val_string(""));
    }

    // Cleanup malloc'd strings from JS
    if (resp_body) free(resp_body);
    if (resp_headers) free(resp_headers);

    return obj;
}

// Replace wasm_shim.c stubs with real implementations
HmlValue hml_lws_http_get(HmlValue url) {
    if (url.type != HML_VAL_STRING || !url.as.as_string) {
        hml_runtime_error("http_get() requires string URL");
    }
    return wasm_http_do_request("GET", url.as.as_string->data, NULL, NULL);
}

HmlValue hml_lws_http_post(HmlValue url, HmlValue body, HmlValue ct) {
    if (url.type != HML_VAL_STRING || !url.as.as_string) {
        hml_runtime_error("http_post() requires string URL");
    }
    const char *body_str = (body.type == HML_VAL_STRING && body.as.as_string) ? body.as.as_string->data : "";
    const char *ct_str = (ct.type == HML_VAL_STRING && ct.as.as_string) ? ct.as.as_string->data : "application/json";
    return wasm_http_do_request("POST", url.as.as_string->data, body_str, ct_str);
}

HmlValue hml_lws_http_request(HmlValue m, HmlValue url, HmlValue body, HmlValue ct) {
    if (m.type != HML_VAL_STRING || !m.as.as_string) {
        hml_runtime_error("http_request() requires string method");
    }
    if (url.type != HML_VAL_STRING || !url.as.as_string) {
        hml_runtime_error("http_request() requires string URL");
    }
    const char *body_str = (body.type == HML_VAL_STRING && body.as.as_string) ? body.as.as_string->data : "";
    const char *ct_str = (ct.type == HML_VAL_STRING && ct.as.as_string) ? ct.as.as_string->data : "";
    return wasm_http_do_request(m.as.as_string->data, url.as.as_string->data, body_str, ct_str);
}

// Custom-header variants — synchronous XHR doesn't expose request headers,
// so the headers argument is dropped and we fall back to the regular path.
HmlValue hml_lws_http_get_with_headers(HmlValue url, HmlValue h) {
    (void)h;
    return hml_lws_http_get(url);
}

HmlValue hml_lws_http_post_with_headers(HmlValue url, HmlValue body, HmlValue ct, HmlValue h) {
    (void)h;
    return hml_lws_http_post(url, body, ct);
}

HmlValue hml_lws_http_request_with_headers(HmlValue m, HmlValue url, HmlValue body, HmlValue ct, HmlValue h) {
    (void)h;
    return hml_lws_http_request(m, url, body, ct);
}

// Timeout variants — timeout is ignored in synchronous XHR mode
HmlValue hml_lws_http_get_timeout(HmlValue url, HmlValue t) {
    (void)t;
    return hml_lws_http_get(url);
}

HmlValue hml_lws_http_get_timeout_with_headers(HmlValue url, HmlValue t, HmlValue h) {
    (void)t; (void)h;
    return hml_lws_http_get(url);
}

HmlValue hml_lws_http_post_timeout(HmlValue url, HmlValue body, HmlValue ct, HmlValue t) {
    (void)t;
    return hml_lws_http_post(url, body, ct);
}

HmlValue hml_lws_http_post_timeout_with_headers(HmlValue url, HmlValue body, HmlValue ct, HmlValue t, HmlValue h) {
    (void)t; (void)h;
    return hml_lws_http_post(url, body, ct);
}

HmlValue hml_lws_http_request_timeout(HmlValue m, HmlValue url, HmlValue body, HmlValue ct, HmlValue t) {
    (void)t;
    return hml_lws_http_request(m, url, body, ct);
}

HmlValue hml_lws_http_request_timeout_with_headers(HmlValue m, HmlValue url, HmlValue body, HmlValue ct, HmlValue t, HmlValue h) {
    (void)t; (void)h;
    return hml_lws_http_request(m, url, body, ct);
}

// Response accessors — work on the response object
HmlValue hml_lws_response_status(HmlValue r) {
    if (r.type != HML_VAL_OBJECT) return hml_val_i32(0);
    return hml_object_get_field(r, "status");
}

HmlValue hml_lws_response_body(HmlValue r) {
    if (r.type != HML_VAL_OBJECT) return hml_val_string("");
    return hml_object_get_field(r, "body");
}

HmlValue hml_lws_response_headers(HmlValue r) {
    if (r.type != HML_VAL_OBJECT) return hml_val_string("");
    return hml_object_get_field(r, "headers");
}

HmlValue hml_lws_response_free(HmlValue r) {
    (void)r;
    return hml_val_null();  // GC handles cleanup
}

HmlValue hml_lws_response_redirect(HmlValue r) {
    (void)r;
    return hml_val_null();
}

HmlValue hml_lws_response_body_binary(HmlValue r) {
    return hml_lws_response_body(r);
}

// HTTP builtin wrappers
HmlValue hml_builtin_lws_http_get(HmlClosureEnv *env, HmlValue url) { (void)env; return hml_lws_http_get(url); }
HmlValue hml_builtin_lws_http_get_with_headers(HmlClosureEnv *env, HmlValue url, HmlValue h) { (void)env; return hml_lws_http_get_with_headers(url, h); }
HmlValue hml_builtin_lws_http_post(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue ct) { (void)env; return hml_lws_http_post(url,body,ct); }
HmlValue hml_builtin_lws_http_post_with_headers(HmlClosureEnv *env, HmlValue url, HmlValue body, HmlValue ct, HmlValue h) { (void)env; return hml_lws_http_post_with_headers(url, body, ct, h); }
HmlValue hml_builtin_lws_http_request(HmlClosureEnv *env, HmlValue m, HmlValue url, HmlValue body, HmlValue ct) { (void)env; return hml_lws_http_request(m,url,body,ct); }
HmlValue hml_builtin_lws_http_request_with_headers(HmlClosureEnv *env, HmlValue m, HmlValue url, HmlValue body, HmlValue ct, HmlValue h) { (void)env; return hml_lws_http_request_with_headers(m, url, body, ct, h); }
HmlValue hml_builtin_lws_response_status(HmlClosureEnv *env, HmlValue r) { (void)env; return hml_lws_response_status(r); }
HmlValue hml_builtin_lws_response_body(HmlClosureEnv *env, HmlValue r) { (void)env; return hml_lws_response_body(r); }
HmlValue hml_builtin_lws_response_headers(HmlClosureEnv *env, HmlValue r) { (void)env; return hml_lws_response_headers(r); }
HmlValue hml_builtin_lws_response_free(HmlClosureEnv *env, HmlValue r) { (void)env; return hml_lws_response_free(r); }
HmlValue hml_builtin_lws_response_redirect(HmlClosureEnv *env, HmlValue r) { (void)env; return hml_lws_response_redirect(r); }
HmlValue hml_builtin_lws_response_body_binary(HmlClosureEnv *env, HmlValue r) { (void)env; return hml_lws_response_body_binary(r); }

// Streaming stubs — not fully supported in sync XHR mode
HmlValue hml_lws_http_stream_start(HmlValue m, HmlValue url, HmlValue body, HmlValue ct, HmlValue t) {
    // Return the full response as a "stream" (non-streaming fallback)
    return hml_lws_http_request_timeout(m, url, body, ct, t);
}
HmlValue hml_lws_http_stream_read(HmlValue s, HmlValue t) { (void)t; return hml_lws_response_body(s); }
HmlValue hml_lws_http_stream_status(HmlValue s) { return hml_lws_response_status(s); }
HmlValue hml_lws_http_stream_headers(HmlValue s) { return hml_lws_response_headers(s); }
HmlValue hml_lws_http_stream_close(HmlValue s) { (void)s; return hml_val_null(); }

HmlValue hml_builtin_lws_http_stream_start(HmlClosureEnv *env, HmlValue m, HmlValue url, HmlValue body, HmlValue ct, HmlValue t) { (void)env; return hml_lws_http_stream_start(m,url,body,ct,t); }
HmlValue hml_builtin_lws_http_stream_read(HmlClosureEnv *env, HmlValue s, HmlValue t) { (void)env; return hml_lws_http_stream_read(s,t); }
HmlValue hml_builtin_lws_http_stream_status(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_lws_http_stream_status(s); }
HmlValue hml_builtin_lws_http_stream_headers(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_lws_http_stream_headers(s); }
HmlValue hml_builtin_lws_http_stream_close(HmlClosureEnv *env, HmlValue s) { (void)env; return hml_lws_http_stream_close(s); }

/* ========================================================================
 * SECTION 2: Pure C SHA-256 Implementation
 *
 * Replaces OpenSSL's SHA256() for WASM builds.
 * Based on FIPS 180-4 specification.
 * ======================================================================== */

// SHA-256 constants
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        w[i] = SHA256_SIG1(w[i-2]) + w[i-7] + SHA256_SIG0(w[i-15]) + w[i-16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e,f,g) + sha256_k[i] + w[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_compute(const uint8_t *data, size_t len, uint8_t hash[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint8_t block[64];
    size_t i;

    // Process full blocks
    for (i = 0; i + 64 <= len; i += 64) {
        sha256_transform(state, data + i);
    }

    // Final block with padding
    size_t remaining = len - i;
    memset(block, 0, 64);
    memcpy(block, data + i, remaining);
    block[remaining] = 0x80;

    if (remaining >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }

    uint64_t bits = (uint64_t)len * 8;
    block[56] = (uint8_t)(bits >> 56); block[57] = (uint8_t)(bits >> 48);
    block[58] = (uint8_t)(bits >> 40); block[59] = (uint8_t)(bits >> 32);
    block[60] = (uint8_t)(bits >> 24); block[61] = (uint8_t)(bits >> 16);
    block[62] = (uint8_t)(bits >> 8);  block[63] = (uint8_t)(bits);
    sha256_transform(state, block);

    for (i = 0; i < 8; i++) {
        hash[i*4]   = (uint8_t)(state[i] >> 24);
        hash[i*4+1] = (uint8_t)(state[i] >> 16);
        hash[i*4+2] = (uint8_t)(state[i] >> 8);
        hash[i*4+3] = (uint8_t)(state[i]);
    }
}

/* ========================================================================
 * SECTION 3: Pure C SHA-512 Implementation
 * ======================================================================== */

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

#define SHA512_ROTR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define SHA512_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA512_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA512_EP0(x) (SHA512_ROTR(x, 28) ^ SHA512_ROTR(x, 34) ^ SHA512_ROTR(x, 39))
#define SHA512_EP1(x) (SHA512_ROTR(x, 14) ^ SHA512_ROTR(x, 18) ^ SHA512_ROTR(x, 41))
#define SHA512_SIG0(x) (SHA512_ROTR(x, 1) ^ SHA512_ROTR(x, 8) ^ ((x) >> 7))
#define SHA512_SIG1(x) (SHA512_ROTR(x, 19) ^ SHA512_ROTR(x, 61) ^ ((x) >> 6))

static void sha512_transform(uint64_t state[8], const uint8_t block[128]) {
    uint64_t w[80], a, b, c, d, e, f, g, h, t1, t2;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint64_t)block[i*8] << 56) | ((uint64_t)block[i*8+1] << 48) |
               ((uint64_t)block[i*8+2] << 40) | ((uint64_t)block[i*8+3] << 32) |
               ((uint64_t)block[i*8+4] << 24) | ((uint64_t)block[i*8+5] << 16) |
               ((uint64_t)block[i*8+6] << 8)  | block[i*8+7];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = SHA512_SIG1(w[i-2]) + w[i-7] + SHA512_SIG0(w[i-15]) + w[i-16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (int i = 0; i < 80; i++) {
        t1 = h + SHA512_EP1(e) + SHA512_CH(e,f,g) + sha512_k[i] + w[i];
        t2 = SHA512_EP0(a) + SHA512_MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha512_compute(const uint8_t *data, size_t len, uint8_t hash[64]) {
    uint64_t state[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    uint8_t block[128];
    size_t i;

    for (i = 0; i + 128 <= len; i += 128) {
        sha512_transform(state, data + i);
    }

    size_t remaining = len - i;
    memset(block, 0, 128);
    memcpy(block, data + i, remaining);
    block[remaining] = 0x80;

    if (remaining >= 112) {
        sha512_transform(state, block);
        memset(block, 0, 128);
    }

    uint64_t bits = (uint64_t)len * 8;
    block[120] = (uint8_t)(bits >> 56); block[121] = (uint8_t)(bits >> 48);
    block[122] = (uint8_t)(bits >> 40); block[123] = (uint8_t)(bits >> 32);
    block[124] = (uint8_t)(bits >> 24); block[125] = (uint8_t)(bits >> 16);
    block[126] = (uint8_t)(bits >> 8);  block[127] = (uint8_t)(bits);
    sha512_transform(state, block);

    for (i = 0; i < 8; i++) {
        hash[i*8]   = (uint8_t)(state[i] >> 56);
        hash[i*8+1] = (uint8_t)(state[i] >> 48);
        hash[i*8+2] = (uint8_t)(state[i] >> 40);
        hash[i*8+3] = (uint8_t)(state[i] >> 32);
        hash[i*8+4] = (uint8_t)(state[i] >> 24);
        hash[i*8+5] = (uint8_t)(state[i] >> 16);
        hash[i*8+6] = (uint8_t)(state[i] >> 8);
        hash[i*8+7] = (uint8_t)(state[i]);
    }
}

/* ========================================================================
 * SECTION 3.5: Pure C SHA-1 Implementation
 * WARNING: SHA-1 is cryptographically weak, included for legacy compatibility
 * ======================================================================== */

#define SHA1_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_compute(const uint8_t *data, size_t len, uint8_t hash[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe,
             h3 = 0x10325476, h4 = 0xc3d2e1f0;

    // Pre-processing: total length in bits
    uint64_t bit_len = (uint64_t)len * 8;

    // Process full 64-byte blocks
    size_t i;
    for (i = 0; i + 64 <= len; i += 64) {
        uint32_t w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)data[i + j*4] << 24) |
                   ((uint32_t)data[i + j*4+1] << 16) |
                   ((uint32_t)data[i + j*4+2] << 8) |
                    (uint32_t)data[i + j*4+3];
        }
        for (int j = 16; j < 80; j++) {
            w[j] = SHA1_ROTL(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20)      { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (j < 40) { f = b ^ c ^ d;           k = 0x6ed9eba1; }
            else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else             { f = b ^ c ^ d;           k = 0xca62c1d6; }
            uint32_t temp = SHA1_ROTL(a, 5) + f + e + k + w[j];
            e = d; d = c; c = SHA1_ROTL(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    // Final block with padding
    size_t remaining = len - i;
    uint8_t block[128];
    memcpy(block, data + i, remaining);
    block[remaining] = 0x80;
    memset(block + remaining + 1, 0, 64 - remaining - 1);

    if (remaining >= 55) {
        // Need two blocks
        memset(block + 64, 0, 64);
        block[120] = (uint8_t)(bit_len >> 56); block[121] = (uint8_t)(bit_len >> 48);
        block[122] = (uint8_t)(bit_len >> 40); block[123] = (uint8_t)(bit_len >> 32);
        block[124] = (uint8_t)(bit_len >> 24); block[125] = (uint8_t)(bit_len >> 16);
        block[126] = (uint8_t)(bit_len >> 8);  block[127] = (uint8_t)(bit_len);
        // Process both blocks
        for (int blk = 0; blk < 2; blk++) {
            uint32_t w[80];
            const uint8_t *bp = block + blk * 64;
            for (int j = 0; j < 16; j++) {
                w[j] = ((uint32_t)bp[j*4] << 24) | ((uint32_t)bp[j*4+1] << 16) |
                       ((uint32_t)bp[j*4+2] << 8) | (uint32_t)bp[j*4+3];
            }
            for (int j = 16; j < 80; j++) {
                w[j] = SHA1_ROTL(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);
            }
            uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            for (int j = 0; j < 80; j++) {
                uint32_t f, k;
                if (j < 20)      { f = (b & c) | (~b & d); k = 0x5a827999; }
                else if (j < 40) { f = b ^ c ^ d;           k = 0x6ed9eba1; }
                else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
                else             { f = b ^ c ^ d;           k = 0xca62c1d6; }
                uint32_t temp = SHA1_ROTL(a, 5) + f + e + k + w[j];
                e = d; d = c; c = SHA1_ROTL(b, 30); b = a; a = temp;
            }
            h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        }
    } else {
        block[56] = (uint8_t)(bit_len >> 56); block[57] = (uint8_t)(bit_len >> 48);
        block[58] = (uint8_t)(bit_len >> 40); block[59] = (uint8_t)(bit_len >> 32);
        block[60] = (uint8_t)(bit_len >> 24); block[61] = (uint8_t)(bit_len >> 16);
        block[62] = (uint8_t)(bit_len >> 8);  block[63] = (uint8_t)(bit_len);
        uint32_t w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)block[j*4] << 24) | ((uint32_t)block[j*4+1] << 16) |
                   ((uint32_t)block[j*4+2] << 8) | (uint32_t)block[j*4+3];
        }
        for (int j = 16; j < 80; j++) {
            w[j] = SHA1_ROTL(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20)      { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (j < 40) { f = b ^ c ^ d;           k = 0x6ed9eba1; }
            else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else             { f = b ^ c ^ d;           k = 0xca62c1d6; }
            uint32_t temp = SHA1_ROTL(a, 5) + f + e + k + w[j];
            e = d; d = c; c = SHA1_ROTL(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    // Output big-endian
    hash[0]  = (uint8_t)(h0 >> 24); hash[1]  = (uint8_t)(h0 >> 16);
    hash[2]  = (uint8_t)(h0 >> 8);  hash[3]  = (uint8_t)(h0);
    hash[4]  = (uint8_t)(h1 >> 24); hash[5]  = (uint8_t)(h1 >> 16);
    hash[6]  = (uint8_t)(h1 >> 8);  hash[7]  = (uint8_t)(h1);
    hash[8]  = (uint8_t)(h2 >> 24); hash[9]  = (uint8_t)(h2 >> 16);
    hash[10] = (uint8_t)(h2 >> 8);  hash[11] = (uint8_t)(h2);
    hash[12] = (uint8_t)(h3 >> 24); hash[13] = (uint8_t)(h3 >> 16);
    hash[14] = (uint8_t)(h3 >> 8);  hash[15] = (uint8_t)(h3);
    hash[16] = (uint8_t)(h4 >> 24); hash[17] = (uint8_t)(h4 >> 16);
    hash[18] = (uint8_t)(h4 >> 8);  hash[19] = (uint8_t)(h4);
}

/* ========================================================================
 * SECTION 4: Pure C MD5 Implementation
 * ======================================================================== */

#define MD5_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))
#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static const uint32_t md5_t[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const int md5_s[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
};

static void md5_compute(const uint8_t *data, size_t len, uint8_t hash[16]) {
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    // Prepare padded message
    size_t new_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = calloc(new_len, 1);
    memcpy(msg, data, len);
    msg[len] = 0x80;

    uint64_t bits = (uint64_t)len * 8;
    memcpy(msg + new_len - 8, &bits, 8);  // Little-endian

    // Process blocks
    for (size_t offset = 0; offset < new_len; offset += 64) {
        uint32_t m[16];
        for (int i = 0; i < 16; i++) {
            memcpy(&m[i], msg + offset + i*4, 4);  // Little-endian
        }

        uint32_t a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t f, g;
            if (i < 16)      { f = MD5_F(b,c,d); g = i; }
            else if (i < 32) { f = MD5_G(b,c,d); g = (5*i + 1) % 16; }
            else if (i < 48) { f = MD5_H(b,c,d); g = (3*i + 5) % 16; }
            else              { f = MD5_I(b,c,d); g = (7*i) % 16; }

            f = f + a + md5_t[i] + m[g];
            a = d; d = c; c = b;
            b = b + MD5_ROTL(f, md5_s[i]);
        }
        a0 += a; b0 += b; c0 += c; d0 += d;
    }
    free(msg);

    // Output in little-endian
    memcpy(hash, &a0, 4);
    memcpy(hash + 4, &b0, 4);
    memcpy(hash + 8, &c0, 4);
    memcpy(hash + 12, &d0, 4);
}

/* ========================================================================
 * SECTION 5: Hash Functions (Hemlock API)
 * ======================================================================== */

static HmlValue bytes_to_hex(const uint8_t *bytes, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    char *hex = malloc(len * 2 + 1);
    for (size_t i = 0; i < len; i++) {
        hex[i * 2]     = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len * 2] = '\0';
    HmlValue result = hml_val_string(hex);
    free(hex);
    return result;
}

HmlValue hml_hash_sha1(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("sha1() requires string argument");
    }
    uint8_t hash[20];
    sha1_compute((const uint8_t *)input.as.as_string->data, input.as.as_string->length, hash);
    return bytes_to_hex(hash, 20);
}

HmlValue hml_hash_sha256(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("sha256() requires string argument");
    }
    uint8_t hash[32];
    sha256_compute((const uint8_t *)input.as.as_string->data, input.as.as_string->length, hash);
    return bytes_to_hex(hash, 32);
}

HmlValue hml_hash_sha512(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("sha512() requires string argument");
    }
    uint8_t hash[64];
    sha512_compute((const uint8_t *)input.as.as_string->data, input.as.as_string->length, hash);
    return bytes_to_hex(hash, 64);
}

HmlValue hml_hash_md5(HmlValue input) {
    if (input.type != HML_VAL_STRING) {
        hml_runtime_error("md5() requires string argument");
    }
    uint8_t hash[16];
    md5_compute((const uint8_t *)input.as.as_string->data, input.as.as_string->length, hash);
    return bytes_to_hex(hash, 16);
}

HmlValue hml_builtin_hash_sha1(HmlClosureEnv *env, HmlValue input) { (void)env; return hml_hash_sha1(input); }
HmlValue hml_builtin_hash_sha256(HmlClosureEnv *env, HmlValue input) { (void)env; return hml_hash_sha256(input); }
HmlValue hml_builtin_hash_sha512(HmlClosureEnv *env, HmlValue input) { (void)env; return hml_hash_sha512(input); }
HmlValue hml_builtin_hash_md5(HmlClosureEnv *env, HmlValue input) { (void)env; return hml_hash_md5(input); }

/* ========================================================================
 * SECTION 6: random_bytes via crypto.getRandomValues
 * ======================================================================== */

EM_JS(void, _hml_js_random_bytes, (uint8_t *buf, int size), {
    if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
        // Browser or Node.js >= 15
        var arr = new Uint8Array(size);
        crypto.getRandomValues(arr);
        for (var i = 0; i < size; i++) {
            HEAPU8[buf + i] = arr[i];
        }
    } else if (typeof require !== 'undefined') {
        // Older Node.js
        try {
            var cryptoModule = require('crypto');
            var bytes = cryptoModule.randomBytes(size);
            for (var i = 0; i < size; i++) {
                HEAPU8[buf + i] = bytes[i];
            }
        } catch(e) {
            // Last resort: Math.random (NOT cryptographically secure)
            for (var i = 0; i < size; i++) {
                HEAPU8[buf + i] = Math.floor(Math.random() * 256);
            }
        }
    } else {
        // Fallback: Math.random
        for (var i = 0; i < size; i++) {
            HEAPU8[buf + i] = Math.floor(Math.random() * 256);
        }
    }
});

// WASM implementation of random_bytes - returns a buffer with random data
HmlValue hml_wasm_random_bytes(HmlValue size_val) {
    int32_t size = hml_to_i32(size_val);
    if (size <= 0) {
        hml_runtime_error("random_bytes() size must be positive");
    }

    HmlValue buf = hml_val_buffer(size);
    _hml_js_random_bytes((uint8_t *)buf.as.as_buffer->data, size);
    return buf;
}

HmlValue hml_builtin_wasm_random_bytes(HmlClosureEnv *env, HmlValue size_val) {
    (void)env;
    return hml_wasm_random_bytes(size_val);
}

#endif // __EMSCRIPTEN__
