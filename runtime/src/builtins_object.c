/*
 * Hemlock Runtime Library - Object Builtins
 *
 * This file implements object-related operations:
 * - Object field get/set
 * - Object keys/values
 * - Hash table operations
 */

#include "builtins_internal.h"

// ========== OBJECT OPERATIONS ==========

// DJB2 hash function - fast and good distribution for field names
static uint32_t djb2_hash(const char *str) {
    uint32_t hash = 5381;  // HML_DJB2_HASH_SEED
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash;
}

// Rebuild hash table for object (called after adding fields or on first lookup)
static void object_hash_rebuild(HmlObject *obj) {
    // Free existing hash table
    free(obj->hash_table);

    // Create hash table with 2x capacity for good load factor
    int new_capacity = obj->num_fields < 4 ? 8 : obj->num_fields * 2;
    obj->hash_table = malloc(sizeof(int) * new_capacity);
    obj->hash_capacity = new_capacity;

    // Initialize all slots to -1 (empty)
    for (int i = 0; i < new_capacity; i++) {
        obj->hash_table[i] = -1;
    }

    // Rehash all existing fields (using unified field storage)
    for (int i = 0; i < obj->num_fields; i++) {
        uint32_t hash = djb2_hash(obj->fields[i].name);
        int slot = hash % new_capacity;

        // Linear probing to find empty slot
        while (obj->hash_table[slot] != -1) {
            slot = (slot + 1) % new_capacity;
        }
        obj->hash_table[slot] = i;
    }
}

// Look up field index by name using hash table, returns -1 if not found
static int object_lookup_field(HmlObject *obj, const char *name) {
    // Lazy hash table creation: if no hash table and we have fields, build it
    if ((!obj->hash_table || obj->hash_capacity == 0) && obj->num_fields > 0) {
        object_hash_rebuild(obj);
    }

    if (!obj->hash_table || obj->hash_capacity == 0) {
        return -1;  // Empty object
    }

    uint32_t hash = djb2_hash(name);
    int slot = hash % obj->hash_capacity;
    int start_slot = slot;

    // Linear probing (using unified field storage)
    while (obj->hash_table[slot] != -1) {
        int idx = obj->hash_table[slot];
        if (strcmp(obj->fields[idx].name, name) == 0) {
            return idx;  // Found
        }
        slot = (slot + 1) % obj->hash_capacity;
        if (slot == start_slot) {
            break;  // Full circle, not found
        }
    }
    return -1;  // Not found
}

HmlValue hml_object_get_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Property access requires object (trying to get '%s' from type %s)",
                field, hml_typeof_str(obj));
    }

    HmlObject *o = obj.as.as_object;
    int idx = object_lookup_field(o, field);
    if (idx >= 0) {
        HmlValue result = o->fields[idx].value;
        hml_retain(&result);
        return result;
    }

    return hml_val_null();  // Field not found
}

// Get field from object - throws error if field not found (for strict property access)
HmlValue hml_object_get_field_required(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Property access requires object (trying to get '%s' from type %s)",
                field, hml_typeof_str(obj));
    }

    HmlObject *o = obj.as.as_object;
    int idx = object_lookup_field(o, field);
    if (idx >= 0) {
        HmlValue result = o->fields[idx].value;
        hml_retain(&result);
        return result;
    }

    hml_runtime_error("Object has no field '%s' (use ?. for optional access)", field);
    return hml_val_null();  // Unreachable but needed for compiler
}

void hml_object_set_field(HmlValue obj, const char *field, HmlValue val) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Property assignment requires object");
    }

    HmlObject *o = obj.as.as_object;

    // Check if field exists using hash lookup
    int idx = object_lookup_field(o, field);
    if (idx >= 0) {
        hml_release(&o->fields[idx].value);
        o->fields[idx].value = val;
        hml_retain(&o->fields[idx].value);
        return;
    }

    // Add new field - grow storage if needed
    if (o->num_fields >= o->capacity) {
        // SECURITY: Check for integer overflow before doubling capacity
        if (o->capacity > INT_MAX / 2) {
            hml_runtime_error("Object field capacity overflow");
        }
        int new_cap = (o->capacity == 0) ? 4 : o->capacity * 2;
        HmlFieldEntry *new_fields;
        if (o->is_pooled && o->capacity > 0) {
            // Pooled object growing beyond pool storage — can't realloc pool memory
            new_fields = malloc(new_cap * sizeof(HmlFieldEntry));
            if (new_fields) {
                memcpy(new_fields, o->fields, o->num_fields * sizeof(HmlFieldEntry));
            }
        } else {
            new_fields = realloc(o->fields, new_cap * sizeof(HmlFieldEntry));
        }
        if (!new_fields) {
            hml_runtime_error("Out of memory expanding object fields");
        }
        o->fields = new_fields;
        o->capacity = new_cap;
    }

    o->fields[o->num_fields].name = strdup(field);
    o->fields[o->num_fields].value = val;
    hml_retain(&o->fields[o->num_fields].value);
    o->num_fields++;

    // Eagerly rebuild hash table to avoid thread-unsafe lazy initialization.
    // Without this, concurrent readers race on the lazy init in object_lookup_field.
    object_hash_rebuild(o);
}

int hml_object_has_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        return 0;
    }

    HmlObject *o = obj.as.as_object;
    return object_lookup_field(o, field) >= 0;
}

// Delete a field from object, returns 1 if deleted, 0 if not found
int hml_object_delete_field(HmlValue obj, const char *field) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        return 0;
    }

    HmlObject *o = obj.as.as_object;
    int found_index = object_lookup_field(o, field);

    if (found_index == -1) {
        return 0;  // Not found
    }

    // Release the value and free the field name (unified storage)
    hml_release(&o->fields[found_index].value);
    free(o->fields[found_index].name);

    // Shift remaining field entries down (single memmove for unified storage)
    for (int i = found_index; i < o->num_fields - 1; i++) {
        o->fields[i] = o->fields[i + 1];
    }

    o->num_fields--;

    // Eagerly rebuild hash table to avoid thread-unsafe lazy initialization
    object_hash_rebuild(o);

    return 1;  // Deleted
}

// Convert a value to a string key for object property access.
// Returns true if the value can be coerced (integers, floats, bools, runes).
int hml_value_coerce_to_key(HmlValue val, char *buf, size_t buf_size) {
    // Check rune before integer since hml_is_integer() includes runes
    if (val.type == HML_VAL_RUNE) {
        int len = utf8_encode_rune(val.as.as_rune, buf);
        buf[len] = '\0';
        return 1;
    }
    if (val.type == HML_VAL_BOOL) {
        snprintf(buf, buf_size, "%s", val.as.as_bool ? "true" : "false");
        return 1;
    }
    if (hml_is_integer(val)) {
        snprintf(buf, buf_size, "%" PRId64, hml_to_i64(val));
        return 1;
    }
    if (hml_is_float_type(val)) {
        snprintf(buf, buf_size, "%.17g", hml_to_f64(val));
        return 1;
    }
    return 0;
}

// Convert non-string key to string and get field from object
HmlValue hml_object_get_field_coerce(HmlValue obj, HmlValue key) {
    char key_buf[64];
    if (hml_value_coerce_to_key(key, key_buf, sizeof(key_buf))) {
        return hml_object_get_field(obj, key_buf);
    }
    return hml_val_null();
}

// Convert non-string key to string and set field on object
void hml_object_set_field_coerce(HmlValue obj, HmlValue key, HmlValue val) {
    char key_buf[64];
    if (hml_value_coerce_to_key(key, key_buf, sizeof(key_buf))) {
        hml_object_set_field(obj, key_buf, val);
    }
}

// Get number of fields in object
int hml_object_num_fields(HmlValue obj) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        return 0;
    }
    return obj.as.as_object->num_fields;
}

// Get field name at index
HmlValue hml_object_key_at(HmlValue obj, int index) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Object key access requires object");
    }
    HmlObject *o = obj.as.as_object;
    if (index < 0 || index >= o->num_fields) {
        hml_runtime_error("Object key index out of bounds");
    }
    return hml_val_string(o->fields[index].name);
}

// Get field value at index
HmlValue hml_object_value_at(HmlValue obj, int index) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Object value access requires object");
    }
    HmlObject *o = obj.as.as_object;
    if (index < 0 || index >= o->num_fields) {
        hml_runtime_error("Object value index out of bounds");
    }
    HmlValue result = o->fields[index].value;
    hml_retain(&result);
    return result;
}

// Get all keys of an object as an array
HmlValue hml_object_keys(HmlValue obj) {
    if (obj.type != HML_VAL_OBJECT || !obj.as.as_object) {
        hml_runtime_error("Object has no method 'keys'");
    }
    HmlObject *o = obj.as.as_object;

    // Create a new array to hold the keys
    HmlValue arr = hml_val_array();

    // Add each field name to the array (using unified storage)
    for (int i = 0; i < o->num_fields; i++) {
        hml_array_push(arr, hml_val_string(o->fields[i].name));
    }

    return arr;
}

