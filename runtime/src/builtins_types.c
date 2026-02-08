/*
 * Hemlock Runtime Library - Type Definition Builtins
 *
 * This file implements:
 * - Type definitions (duck typing)
 * - Enum type registry
 */

#include "builtins_internal.h"
#include <limits.h>

// ========== TYPE DEFINITIONS (DUCK TYPING) ==========

// Type registry
static HmlTypeDef *g_type_registry = NULL;
static int g_type_count = 0;
static int g_type_capacity = 0;

void hml_register_type(const char *name, HmlTypeField *fields, int num_fields) {
    // Initialize registry if needed
    if (g_type_registry == NULL) {
        g_type_capacity = 16;
        g_type_registry = malloc(sizeof(HmlTypeDef) * g_type_capacity);
    }

    // Grow if needed
    if (g_type_count >= g_type_capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (g_type_capacity > INT_MAX / 2) {
            hml_runtime_error("Type registry capacity overflow");
        }
        int new_capacity = g_type_capacity * 2;
        HmlTypeDef *new_registry = realloc(g_type_registry, sizeof(HmlTypeDef) * (size_t)new_capacity);
        if (!new_registry) {
            hml_runtime_error("Out of memory expanding type registry");
        }
        g_type_registry = new_registry;
        g_type_capacity = new_capacity;
    }

    // Add type definition
    HmlTypeDef *type = &g_type_registry[g_type_count++];
    type->name = strdup(name);
    type->num_fields = num_fields;
    type->fields = malloc(sizeof(HmlTypeField) * num_fields);

    for (int i = 0; i < num_fields; i++) {
        type->fields[i].name = strdup(fields[i].name);
        type->fields[i].type_kind = fields[i].type_kind;
        type->fields[i].is_optional = fields[i].is_optional;
        type->fields[i].default_value = fields[i].default_value;
        hml_retain(&type->fields[i].default_value);
    }
}

HmlTypeDef* hml_lookup_type(const char *name) {
    for (int i = 0; i < g_type_count; i++) {
        if (strcmp(g_type_registry[i].name, name) == 0) {
            return &g_type_registry[i];
        }
    }
    return NULL;
}

HmlValue hml_validate_object_type(HmlValue obj, const char *type_name) {
    // First check if this is actually an enum type - if so, delegate to enum validation
    int num_variants = 0;
    const int32_t *enum_values = hml_lookup_enum(type_name, &num_variants);
    if (enum_values) {
        // This is an enum type - delegate to enum validation
        return hml_validate_enum_value(obj, type_name);
    }

    if (obj.type != HML_VAL_OBJECT) {
        fprintf(stderr, "Error: Expected object for type '%s', got %s\n",
                type_name, hml_typeof(obj));
        exit(1);
    }

    HmlTypeDef *type = hml_lookup_type(type_name);
    if (!type) {
        fprintf(stderr, "Error: Unknown type '%s'\n", type_name);
        exit(1);
    }

    HmlObject *o = obj.as.as_object;

    // Check each required field
    for (int i = 0; i < type->num_fields; i++) {
        HmlTypeField *field = &type->fields[i];

        // Find field in object
        int found = 0;
        for (int j = 0; j < o->num_fields; j++) {
            if (strcmp(o->fields[j].name, field->name) == 0) {
                found = 1;
                // Type check if field has a specific type
                if (field->type_kind >= 0) {
                    HmlValue val = o->fields[j].value;
                    int type_ok = 0;

                    switch (field->type_kind) {
                        case HML_VAL_I8: case HML_VAL_I16: case HML_VAL_I32: case HML_VAL_I64:
                        case HML_VAL_U8: case HML_VAL_U16: case HML_VAL_U32: case HML_VAL_U64:
                            type_ok = (val.type >= HML_VAL_I8 && val.type <= HML_VAL_U64);
                            break;
                        case HML_VAL_F32: case HML_VAL_F64:
                            type_ok = (val.type == HML_VAL_F32 || val.type == HML_VAL_F64);
                            break;
                        case HML_VAL_BOOL:
                            type_ok = (val.type == HML_VAL_BOOL);
                            break;
                        case HML_VAL_STRING:
                            type_ok = (val.type == HML_VAL_STRING);
                            break;
                        default:
                            type_ok = 1;  // Accept any type
                            break;
                    }

                    if (!type_ok) {
                        fprintf(stderr, "Error: Field '%s' has wrong type for '%s'\n",
                                field->name, type_name);
                        exit(1);
                    }
                }
                break;
            }
        }

        if (!found) {
            if (field->is_optional) {
                // Add default value
                hml_object_set_field(obj, field->name, field->default_value);
            } else {
                fprintf(stderr, "Error: Object missing required field '%s' for type '%s'\n",
                        field->name, type_name);
                exit(1);
            }
        }
    }

    // Set the object's type name
    if (o->type_name) free(o->type_name);
    o->type_name = strdup(type_name);

    return obj;
}

// ========== ENUM TYPE REGISTRY ==========

typedef struct {
    char *name;
    int32_t *variant_values;
    int num_variants;
} HmlEnumDef;

static HmlEnumDef g_enum_registry[256];
static int g_enum_count = 0;

void hml_register_enum(const char *name, const int32_t *variant_values, int num_variants) {
    if (g_enum_count >= 256) {
        fprintf(stderr, "Error: Too many enum types registered\n");
        exit(1);
    }

    // Check if already registered (update if so)
    for (int i = 0; i < g_enum_count; i++) {
        if (strcmp(g_enum_registry[i].name, name) == 0) {
            // Update existing
            free(g_enum_registry[i].variant_values);
            g_enum_registry[i].variant_values = malloc(sizeof(int32_t) * num_variants);
            memcpy(g_enum_registry[i].variant_values, variant_values, sizeof(int32_t) * num_variants);
            g_enum_registry[i].num_variants = num_variants;
            return;
        }
    }

    // Register new
    g_enum_registry[g_enum_count].name = strdup(name);
    g_enum_registry[g_enum_count].variant_values = malloc(sizeof(int32_t) * num_variants);
    memcpy(g_enum_registry[g_enum_count].variant_values, variant_values, sizeof(int32_t) * num_variants);
    g_enum_registry[g_enum_count].num_variants = num_variants;
    g_enum_count++;
}

const int32_t* hml_lookup_enum(const char *name, int *num_variants) {
    for (int i = 0; i < g_enum_count; i++) {
        if (strcmp(g_enum_registry[i].name, name) == 0) {
            if (num_variants) {
                *num_variants = g_enum_registry[i].num_variants;
            }
            return g_enum_registry[i].variant_values;
        }
    }
    return NULL;
}

HmlValue hml_validate_enum_value(HmlValue val, const char *enum_name) {
    // Enum values must be i32
    if (val.type != HML_VAL_I32) {
        fprintf(stderr, "Runtime error: Expected enum value (i32) for type '%s', got %s\n",
                enum_name, hml_typeof(val));
        exit(1);
    }

    int num_variants = 0;
    const int32_t *variant_values = hml_lookup_enum(enum_name, &num_variants);

    if (!variant_values) {
        fprintf(stderr, "Runtime error: Unknown enum type '%s'\n", enum_name);
        exit(1);
    }

    // Check if value is one of the valid variants
    int32_t value = val.as.as_i32;
    for (int i = 0; i < num_variants; i++) {
        if (variant_values[i] == value) {
            return val;  // Valid variant
        }
    }

    fprintf(stderr, "Runtime error: Value %d is not a valid variant of enum '%s'\n",
            value, enum_name);
    exit(1);
}

// ========== SCHEMA EXTRACTION ==========

// Map HML_VAL_* type kind to JSON Schema type string
static const char* hml_type_kind_to_json_schema_type(int type_kind) {
    switch (type_kind) {
        case HML_VAL_I8:
        case HML_VAL_I16:
        case HML_VAL_I32:
        case HML_VAL_I64:
        case HML_VAL_U8:
        case HML_VAL_U16:
        case HML_VAL_U32:
        case HML_VAL_U64:
            return "integer";
        case HML_VAL_F32:
        case HML_VAL_F64:
            return "number";
        case HML_VAL_BOOL:
            return "boolean";
        case HML_VAL_STRING:
            return "string";
        case HML_VAL_ARRAY:
            return "array";
        case HML_VAL_OBJECT:
            return "object";
        case HML_VAL_NULL:
            return "null";
        default:
            return "any";
    }
}

// Map HML_VAL_* type kind to Hemlock type name string
static const char* hml_type_kind_to_hemlock_type(int type_kind) {
    switch (type_kind) {
        case HML_VAL_I8: return "i8";
        case HML_VAL_I16: return "i16";
        case HML_VAL_I32: return "i32";
        case HML_VAL_I64: return "i64";
        case HML_VAL_U8: return "u8";
        case HML_VAL_U16: return "u16";
        case HML_VAL_U32: return "u32";
        case HML_VAL_U64: return "u64";
        case HML_VAL_F32: return "f32";
        case HML_VAL_F64: return "f64";
        case HML_VAL_BOOL: return "bool";
        case HML_VAL_STRING: return "string";
        case HML_VAL_RUNE: return "rune";
        case HML_VAL_PTR: return "ptr";
        case HML_VAL_BUFFER: return "buffer";
        case HML_VAL_ARRAY: return "array";
        case HML_VAL_OBJECT: return "object";
        case HML_VAL_NULL: return "null";
        default: return "any";
    }
}

// hml_schema(type_name) -> object
// Extracts a JSON Schema-compatible object from the runtime type registry.
HmlValue hml_schema(HmlValue type_name_val) {
    if (type_name_val.type != HML_VAL_STRING) {
        hml_runtime_error("schema() argument must be a string");
    }

    const char *type_name = type_name_val.as.as_string->data;
    HmlTypeDef *type = hml_lookup_type(type_name);

    if (!type) {
        fprintf(stderr, "Runtime error: schema() unknown type '%s'\n", type_name);
        exit(1);
    }

    // Build schema object: { type: "object", name: "TypeName", properties: {...}, required: [...] }
    HmlValue schema = hml_val_object();

    // schema.type = "object"
    hml_object_set_field(schema, "type", hml_val_string("object"));

    // schema.name = type_name
    hml_object_set_field(schema, "name", hml_val_string(type_name));

    // Build properties object
    HmlValue properties = hml_val_object();

    // Build required array
    HmlValue required = hml_val_array();

    for (int i = 0; i < type->num_fields; i++) {
        HmlTypeField *field = &type->fields[i];

        // Build property descriptor
        HmlValue prop = hml_val_object();

        // JSON Schema type
        const char *json_type = (field->type_kind >= 0)
            ? hml_type_kind_to_json_schema_type(field->type_kind)
            : "any";
        hml_object_set_field(prop, "type", hml_val_string(json_type));

        // Hemlock type
        const char *hemlock_type = (field->type_kind >= 0)
            ? hml_type_kind_to_hemlock_type(field->type_kind)
            : "any";
        hml_object_set_field(prop, "hemlock_type", hml_val_string(hemlock_type));

        // Required flag
        hml_object_set_field(prop, "required", hml_val_bool(!field->is_optional));

        // Add property to properties
        hml_object_set_field(properties, field->name, prop);
        hml_release(&prop);

        // Add to required array if not optional
        if (!field->is_optional) {
            HmlValue req_name = hml_val_string(field->name);
            hml_array_push(required, req_name);
            hml_release(&req_name);
        }
    }

    // Set properties and required on schema
    hml_object_set_field(schema, "properties", properties);
    hml_release(&properties);

    hml_object_set_field(schema, "required", required);
    hml_release(&required);

    return schema;
}

// FFI (Foreign Function Interface) operations moved to builtins_ffi.c

