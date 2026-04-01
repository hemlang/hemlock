/*
 * Hemlock Built-in: schema()
 *
 * Extracts a JSON Schema-compatible object from a registered define type.
 * This enables validation of JSON data against Hemlock's structural types.
 *
 * Usage:
 *   define User { name: string, age: i32, email?: string }
 *   let s = schema("User");
 *   // Returns: {
 *   //   type: "object",
 *   //   name: "User",
 *   //   properties: {
 *   //     name: { type: "string", required: true },
 *   //     age: { type: "integer", required: true },
 *   //     email: { type: "string", required: false }
 *   //   },
 *   //   required: ["name", "age"]
 *   // }
 */

#include "internal.h"

// Map a Hemlock TypeKind to a JSON Schema type string
static const char* type_kind_to_json_schema_type(TypeKind kind) {
    switch (kind) {
        case TYPE_I8:
        case TYPE_I16:
        case TYPE_I32:
        case TYPE_I64:
        case TYPE_U8:
        case TYPE_U16:
        case TYPE_U32:
        case TYPE_U64:
            return "integer";
        case TYPE_F32:
        case TYPE_F64:
            return "number";
        case TYPE_BOOL:
            return "boolean";
        case TYPE_STRING:
            return "string";
        case TYPE_ARRAY:
            return "array";
        case TYPE_GENERIC_OBJECT:
        case TYPE_CUSTOM_OBJECT:
            return "object";
        case TYPE_NULL:
            return "null";
        default:
            return "any";
    }
}

// Map a Hemlock TypeKind to its Hemlock type name string
static const char* type_kind_to_hemlock_type(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: return "i8";
        case TYPE_I16: return "i16";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U8: return "u8";
        case TYPE_U16: return "u16";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_RUNE: return "rune";
        case TYPE_PTR: return "ptr";
        case TYPE_BUFFER: return "buffer";
        case TYPE_ARRAY: return "array";
        case TYPE_NULL: return "null";
        case TYPE_GENERIC_OBJECT: return "object";
        default: return "any";
    }
}

// schema(type_name: string) -> object
// Extracts a JSON Schema-compatible object from the type registry.
Value builtin_schema(Value *args, int num_args, ExecutionContext *ctx) {
    if (num_args != 1) {
        fprintf(stderr, "Runtime error: schema() expects 1 argument (type name)\n");
        exit(1);
    }

    if (args[0].type != VAL_STRING) {
        fprintf(stderr, "Runtime error: schema() argument must be a string\n");
        exit(1);
    }

    const char *type_name = args[0].as.as_string->data;
    ObjectType *obj_type = lookup_object_type(type_name);

    if (!obj_type) {
        fprintf(stderr, "Runtime error: schema() unknown type '%s'\n", type_name);
        exit(1);
    }

    // Build the schema object
    // Top-level: { type: "object", name: "TypeName", properties: {...}, required: [...] }
    int num_top_fields = 4;  // type, name, properties, required
    Object *schema = object_new(NULL, num_top_fields);

    // schema.type = "object"
    Value type_val = val_string("object");
    schema->fields[schema->num_fields].name = strdup("type");
    schema->fields[schema->num_fields].value = type_val;
    schema->num_fields++;

    // schema.name = type_name
    Value name_val = val_string(type_name);
    schema->fields[schema->num_fields].name = strdup("name");
    schema->fields[schema->num_fields].value = name_val;
    schema->num_fields++;

    // Build properties object
    int num_fields = obj_type->num_fields;
    Object *properties = object_new(NULL, num_fields > 0 ? num_fields : 1);

    // Build required array
    Array *required = array_new();

    for (int i = 0; i < num_fields; i++) {
        const char *field_name = obj_type->field_names[i];
        Type *field_type = obj_type->field_types[i];
        int is_optional = obj_type->field_optional[i];

        // Build property descriptor: { type: "string", hemlock_type: "string", required: true/false }
        int prop_field_count = 3;
        if (field_type && field_type->kind == TYPE_CUSTOM_OBJECT && field_type->type_name) {
            prop_field_count = 4;  // Add ref field for custom object types
        }
        Object *prop = object_new(NULL, prop_field_count);

        // JSON Schema type
        const char *json_type = "any";
        const char *hemlock_type = "any";
        if (field_type) {
            json_type = type_kind_to_json_schema_type(field_type->kind);
            if (field_type->kind == TYPE_CUSTOM_OBJECT && field_type->type_name) {
                hemlock_type = field_type->type_name;
            } else {
                hemlock_type = type_kind_to_hemlock_type(field_type->kind);
            }
        }

        Value prop_type_val = val_string(json_type);
        prop->fields[prop->num_fields].name = strdup("type");
        prop->fields[prop->num_fields].value = prop_type_val;
        prop->num_fields++;

        Value prop_hml_type_val = val_string(hemlock_type);
        prop->fields[prop->num_fields].name = strdup("hemlock_type");
        prop->fields[prop->num_fields].value = prop_hml_type_val;
        prop->num_fields++;

        Value prop_required_val = val_bool(!is_optional);
        prop->fields[prop->num_fields].name = strdup("required");
        prop->fields[prop->num_fields].value = prop_required_val;
        prop->num_fields++;

        // For custom object types, add a $ref field
        if (field_type && field_type->kind == TYPE_CUSTOM_OBJECT && field_type->type_name) {
            Value ref_val = val_string(field_type->type_name);
            prop->fields[prop->num_fields].name = strdup("$ref");
            prop->fields[prop->num_fields].value = ref_val;
            prop->num_fields++;
        }

        // Add property to properties object
        properties->fields[properties->num_fields].name = strdup(field_name);
        properties->fields[properties->num_fields].value = val_object(prop);
        properties->num_fields++;

        // Add to required array if not optional
        if (!is_optional) {
            Value req_name = val_string(field_name);
            array_push(required, req_name);
            value_release(req_name);  // array_push retains
        }
    }

    // schema.properties = properties
    schema->fields[schema->num_fields].name = strdup("properties");
    schema->fields[schema->num_fields].value = val_object(properties);
    schema->num_fields++;

    // schema.required = required
    // Note: the field takes ownership of the ref_count=1 reference from array_new()
    schema->fields[schema->num_fields].name = strdup("required");
    schema->fields[schema->num_fields].value = val_array(required);
    schema->num_fields++;

    return val_object(schema);
}
