#ifndef HEMLOCK_HASH_H
#define HEMLOCK_HASH_H

#include <stdint.h>
#include "hemlock_limits.h"

// DJB2 hash function - fast and good distribution for string keys
static inline uint32_t hml_hash_string(const char *str) {
    uint32_t hash = HML_DJB2_HASH_SEED;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash;
}

#endif // HEMLOCK_HASH_H
