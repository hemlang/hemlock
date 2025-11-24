#include <stdio.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

int main() {
    printf("Step 1: Creating context\n");
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) {
        printf("Failed to create context\n");
        return 1;
    }
    printf("Step 2: Context created\n");

    printf("Step 3: Initializing keygen\n");
    if (EVP_PKEY_keygen_init(ctx) != 1) {
        printf("EVP_PKEY_keygen_init failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }
    printf("Step 4: Keygen initialized\n");

    printf("Step 5: Setting key size\n");
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) != 1) {
        printf("EVP_PKEY_CTX_set_rsa_keygen_bits failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }
    printf("Step 6: Key size set\n");

    printf("Step 7: Generating key\n");
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(ctx, &pkey) != 1) {
        printf("EVP_PKEY_keygen failed\n");
        EVP_PKEY_CTX_free(ctx);
        return 1;
    }
    printf("Step 8: Key generated successfully!\n");

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    printf("Step 9: Cleanup done\n");

    return 0;
}
