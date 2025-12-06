#include "internal.h"

Value builtin_print(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: print() expects 1 argument\n");
        exit(1);
    }

    print_value(args[0]);
    printf("\n");
    return val_null();
}

Value builtin_string_concat_many(Value *args, int num_args, ExecutionContext *ctx) {
    (void)ctx;  // Unused
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: string_concat_many() expects 1 argument (array of strings)\n");
        exit(1);
    }

    if (args[0].type != VAL_ARRAY) {
        fprintf(stderr, "Runtime error: string_concat_many() expects an array argument\n");
        exit(1);
    }

    Array *arr = args[0].as.as_array;

    // Handle empty array case
    if (arr->length == 0) {
        return val_string("");
    }

    // Extract strings from array
    String **strings = malloc(sizeof(String*) * arr->length);
    if (!strings) {
        fprintf(stderr, "Runtime error: Memory allocation failed\n");
        exit(1);
    }

    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != VAL_STRING) {
            free(strings);
            fprintf(stderr, "Runtime error: string_concat_many() expects all array elements to be strings\n");
            exit(1);
        }
        strings[i] = arr->elements[i].as.as_string;
    }

    // Concatenate all strings
    String *result = string_concat_many(strings, arr->length);
    free(strings);

    Value v = {0};
    v.type = VAL_STRING;
    v.as.as_string = result;
    return v;
}
