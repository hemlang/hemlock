#include <stdio.h>
#include <openssl/evp.h>
#include <openssl/ec.h>

int main() {
    printf("Testing EVP_PKEY_Q_keygen with EC...\n");

    // Try: EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1")
    // The variadic args depend on the key type
    EVP_PKEY *pkey = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1");

    if (pkey == NULL) {
        printf("FAILED: EVP_PKEY_Q_keygen returned NULL\n");
        return 1;
    }

    printf("SUCCESS: EC key generated with EVP_PKEY_Q_keygen!\n");
    EVP_PKEY_free(pkey);
    return 0;
}
