/*
 * Nondeterministic value sources for CBMC harnesses.
 *
 * Functions declared without a body return an unconstrained value under
 * CBMC, so every harness quantifies over all possible inputs of that type.
 */
#ifndef HML_CBMC_NONDET_H
#define HML_CBMC_NONDET_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

int nondet_int(void);
unsigned nondet_unsigned(void);
char nondet_char(void);
uint8_t nondet_u8(void);
uint16_t nondet_u16(void);
uint32_t nondet_u32(void);
uint64_t nondet_u64(void);
int8_t nondet_i8(void);
int16_t nondet_i16(void);
int32_t nondet_i32(void);
int64_t nondet_i64(void);
float nondet_float(void);
double nondet_double(void);

/*
 * Heap buffer of exactly `len + 1` bytes: `len` arbitrary bytes followed by
 * the NUL terminator every Hemlock string carries. Allocating exactly this
 * size lets CBMC's pointer checks catch any read past the allocation.
 */
static inline char *nondet_nul_terminated(int len) {
    char *buf = malloc((size_t)len + 1);
    __CPROVER_assume(buf != NULL);
    for (int i = 0; i < len; i++) buf[i] = nondet_char();
    buf[len] = '\0';
    return buf;
}

#endif
