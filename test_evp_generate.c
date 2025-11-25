#include <stdio.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/param_build.h>

int main() {
    printf("Testing EVP_PKEY_generate (new OpenSSL 3.x API)\n");

    // Try method 1: Using EVP_PKEY_Q_keygen (simplest)
    printf("Method 1: EVP_PKEY_Q_keygen\n");
    EVP_PKEY *pkey1 = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", (size_t)2048);
    if (pkey1) {
        printf("SUCCESS: EVP_PKEY_Q_keygen worked!\n");
        EVP_PKEY_free(pkey1);
    } else {
        printf("FAILED: EVP_PKEY_Q_keygen failed\n");
    }

    // Try method 2: Using EVP_PKEY_generate with params
    printf("\nMethod 2: EVP_PKEY_generate with params\n");
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    if (!ctx) {
        printf("Failed to create context from name\n");
        return 1;
    }

    if (EVP_PKEY_keygen_init(ctx) != 1) {
        printf("keygen_init failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) != 1) {
        printf("set_rsa_keygen_bits failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }

    EVP_PKEY *pkey2 = NULL;
    if (EVP_PKEY_generate(ctx, &pkey2) != 1) {
        printf("EVP_PKEY_generate failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }

    printf("SUCCESS: EVP_PKEY_generate worked!\n");
    EVP_PKEY_free(pkey2);
    EVP_PKEY_CTX_free(ctx);

    return 0;
}
