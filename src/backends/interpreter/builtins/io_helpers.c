#include "internal.h"

Value builtin_print(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1) {
        runtime_error(ctx, "print() expects at least 1 argument"); return val_null();
    }

    for (int i = 0; i < num_args; i++) {
        if (i > 0) printf(" ");
        print_value(args[i]);
    }
    printf("\n");
    fflush(stdout);
    return val_null();
}

Value builtin_write(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args < 1) {
        runtime_error(ctx, "write() expects at least 1 argument"); return val_null();
    }

    for (int i = 0; i < num_args; i++) {
        if (i > 0) printf(" ");
        print_value(args[i]);
    }
    fflush(stdout);
    return val_null();
}

Value builtin_string_concat_many(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        runtime_error(ctx, "string_concat_many() expects 1 argument (array of strings)"); return val_null();
    }

    if (args[0].type != VAL_ARRAY) {
        runtime_error(ctx, "string_concat_many() expects an array argument"); return val_null();
    }

    Array *arr = args[0].as.as_array;

    // Handle empty array case
    if (arr->length == 0) {
        return val_string("");
    }

    // Extract strings from array
    String **strings = malloc(sizeof(String*) * arr->length);
    if (!strings) {
        runtime_error(ctx, "Memory allocation failed"); return val_null();
    }

    for (int i = 0; i < arr->length; i++) {
        if (arr->elements[i].type != VAL_STRING) {
            free(strings);
            runtime_error(ctx, "string_concat_many() expects all array elements to be strings"); return val_null();
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
