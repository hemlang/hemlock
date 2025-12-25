// Test library for FFI struct support
// Compile: gcc -shared -fPIC -o libffi_struct_test.so ffi_struct_test.c

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Simple point struct
typedef struct {
    double x;
    double y;
} Point;

// Mixed types struct
typedef struct {
    int32_t a;
    int32_t b;
    double c;
} Mixed;

// Struct with different integer sizes
typedef struct {
    int8_t val_i8;
    int16_t val_i16;
    int32_t val_i32;
    int64_t val_i64;
} IntSizes;

// Return a point
Point make_point(double x, double y) {
    Point p = { x, y };
    return p;
}

// Add two points
Point add_points(Point a, Point b) {
    Point result = { a.x + b.x, a.y + b.y };
    return result;
}

// Calculate distance from origin
double point_length(Point p) {
    return p.x * p.x + p.y * p.y;  // Returns squared length for simplicity
}

// Sum of mixed struct fields
double mixed_sum(Mixed m) {
    return (double)m.a + (double)m.b + m.c;
}

// Create mixed from values
Mixed make_mixed(int32_t a, int32_t b, double c) {
    Mixed m = { a, b, c };
    return m;
}

// Sum all integer sizes
int64_t sum_int_sizes(IntSizes s) {
    return (int64_t)s.val_i8 + (int64_t)s.val_i16 + (int64_t)s.val_i32 + s.val_i64;
}

// Return integer sizes struct
IntSizes make_int_sizes(int8_t a, int16_t b, int32_t c, int64_t d) {
    IntSizes s = { a, b, c, d };
    return s;
}
