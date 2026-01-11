#ifndef HEMLOCK_AST_SERIALIZE_H
#define HEMLOCK_AST_SERIALIZE_H

#include "frontend/ast.h"
#include <stdint.h>
#include <stdio.h>

// Magic number for .hmlc files ("HMLC" in little-endian)
#define HMLC_MAGIC 0x434C4D48

// Version of the binary format
#define HMLC_VERSION 1

// Flags for compilation options
#define HMLC_FLAG_DEBUG     0x0001  // Include line numbers
#define HMLC_FLAG_COMPRESS  0x0002  // Reserved for future compression

// File header structure
typedef struct {
    uint32_t magic;          // HMLC_MAGIC
    uint16_t version;        // Format version
    uint16_t flags;          // Compilation flags
    uint32_t string_count;   // Number of strings in string table
    uint32_t stmt_count;     // Number of top-level statements
    uint32_t checksum;       // CRC32 of data (0 if not computed)
} HmlcHeader;

// String table for deduplication
typedef struct {
    char **strings;          // Array of strings
    uint32_t *lengths;       // Length of each string
    uint32_t count;          // Number of strings
    uint32_t capacity;       // Allocated capacity
} StringTable;

// Serialization context
typedef struct {
    StringTable strings;     // String table for deduplication
    uint8_t *buffer;         // Output buffer
    size_t buffer_size;      // Current size
    size_t buffer_capacity;  // Allocated capacity
    uint16_t flags;          // Compilation flags
} SerializeContext;

// Deserialization context
typedef struct {
    const uint8_t *data;     // Input data
    size_t data_size;        // Total data size
    size_t offset;           // Current read position
    char **strings;          // Reconstructed string table
    uint32_t string_count;   // Number of strings
    uint16_t flags;          // Flags from header
} DeserializeContext;

// ========== PUBLIC API ==========

/**
 * Serialize an AST program to binary format
 *
 * @param statements Array of top-level statements
 * @param stmt_count Number of statements
 * @param flags      Compilation flags (HMLC_FLAG_*)
 * @param out_size   Output: size of serialized data
 * @return           Allocated buffer containing serialized data (caller must free)
 *                   Returns NULL on error
 */
uint8_t* ast_serialize(Stmt **statements, int stmt_count, uint16_t flags, size_t *out_size);

/**
 * Deserialize binary data to AST
 *
 * @param data       Binary data buffer
 * @param data_size  Size of data buffer
 * @param out_count  Output: number of statements
 * @return           Array of statement pointers (caller must free each and the array)
 *                   Returns NULL on error
 */
Stmt** ast_deserialize(const uint8_t *data, size_t data_size, int *out_count);

/**
 * Serialize AST to a file
 *
 * @param filename   Output file path
 * @param statements Array of top-level statements
 * @param stmt_count Number of statements
 * @param flags      Compilation flags
 * @return           0 on success, -1 on error
 */
int ast_serialize_to_file(const char *filename, Stmt **statements, int stmt_count, uint16_t flags);

/**
 * Deserialize AST from a file
 *
 * @param filename   Input file path
 * @param out_count  Output: number of statements
 * @return           Array of statement pointers (caller must free)
 *                   Returns NULL on error
 */
Stmt** ast_deserialize_from_file(const char *filename, int *out_count);

/**
 * Check if a file is a valid .hmlc file
 *
 * @param filename   File path to check
 * @return           1 if valid .hmlc, 0 otherwise
 */
int is_hmlc_file(const char *filename);

/**
 * Check if data buffer contains valid .hmlc data
 *
 * @param data       Data buffer
 * @param data_size  Size of buffer
 * @return           1 if valid .hmlc, 0 otherwise
 */
int is_hmlc_data(const uint8_t *data, size_t data_size);

#endif // HEMLOCK_AST_SERIALIZE_H
