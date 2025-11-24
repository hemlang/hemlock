#include <stdio.h>
#include <openssl/evp.h>
#include <openssl/ec.h>

int main() {
    // Try generating EC key with EVP_PKEY_Q_keygen
    // Note: For EC keys, you can't use bits parameter like RSA
    // Instead, use group name parameter

    // Method: Use curve name directly
    printf("Testing EC key generation with curve name...\n");

    // Note: EVP_PKEY_Q_keygen doesn't support EC parameters
    // We need to use a different approach

    // Let's use EVP_PKEY_CTX_new_from_name with "EC" and then set parameters
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (!ctx) {
        printf("Failed to create context\n");
        return 1;
    }

    if (EVP_PKEY_keygen_init(ctx) != 1) {
        printf("keygen_init failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }

    // Set group name (P-256)
    if (EVP_PKEY_CTX_set_group_name(ctx, "prime256v1") != 1) {
        printf("set_group_name failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }

    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_generate(ctx, &pkey) != 1) {
        printf("EVP_PKEY_generate failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }

    printf("SUCCESS: EC key generated!\n");

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);

    return 0;
}
