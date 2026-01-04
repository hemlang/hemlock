/*
 * Hemlock Profiler
 *
 * CPU time profiling, memory tracking, and call graph generation
 * for the Hemlock interpreter.
 */

#ifndef HEMLOCK_PROFILER_H
#define HEMLOCK_PROFILER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

// Forward declaration and typedef (compatible with internal.h)
typedef struct ProfilerState ProfilerState;

// ========== PROFILER CONFIGURATION ==========

// Maximum number of unique functions to track
#define HML_PROFILER_MAX_FUNCTIONS 4096

// Maximum number of unique allocation sites
#define HML_PROFILER_MAX_ALLOC_SITES 1024

// Maximum call stack depth for flamegraph sampling
#define HML_PROFILER_MAX_STACK_DEPTH 256

// Hash table size for function lookups (power of 2)
#define HML_PROFILER_HASH_SIZE 1024

// ========== PROFILER MODES ==========

typedef enum {
    PROFILE_MODE_CPU,        // Time spent in functions
    PROFILE_MODE_MEMORY,     // Memory allocations
    PROFILE_MODE_CALLS,      // Call counts only (minimal overhead)
} ProfileMode;

// ========== OUTPUT FORMATS ==========

typedef enum {
    PROFILE_OUTPUT_TEXT,       // Human-readable text summary
    PROFILE_OUTPUT_JSON,       // JSON format for tooling
    PROFILE_OUTPUT_FLAMEGRAPH, // Collapsed format for flamegraph.pl
} ProfileOutputFormat;

// ========== FUNCTION STATS ==========

typedef struct {
    const char *name;           // Function name (borrowed pointer)
    const char *source_file;    // Source file (borrowed pointer)
    int line;                   // Definition line

    // Timing stats
    uint64_t total_time_ns;     // Total time spent (including callees)
    uint64_t self_time_ns;      // Time spent excluding callees
    uint64_t call_count;        // Number of times called
    uint64_t max_time_ns;       // Longest single call
    uint64_t min_time_ns;       // Shortest single call

    // Memory stats (when in memory mode)
    uint64_t alloc_bytes;       // Total bytes allocated
    uint64_t alloc_count;       // Number of allocations

    // Hash chain for collision resolution
    uint32_t hash;
    int next;                   // Next entry in chain (-1 if none)
} FunctionStats;

// ========== ALLOCATION SITE ==========

typedef struct {
    const char *source_file;    // Source file (borrowed pointer)
    int line;                   // Line number

    uint64_t total_bytes;       // Total bytes allocated here
    uint64_t alloc_count;       // Number of allocations
    uint64_t current_bytes;     // Currently live bytes (for leak detection)
    uint64_t max_bytes;         // Peak live bytes at this site

    // Hash chain
    uint32_t hash;
    int next;
} AllocSite;

// ========== CALL STACK SAMPLE ==========

// For flamegraph generation - captures stack at each function entry
typedef struct {
    int *stack_indices;         // Indices into functions array
    int depth;                  // Current stack depth
    int capacity;               // Allocated capacity
} ProfileStack;

// ========== PROFILER STATE ==========

struct ProfilerState {
    // Configuration
    bool enabled;
    ProfileMode mode;
    ProfileOutputFormat output_format;
    int top_n;                  // Show top N entries (0 = all)

    // Function stats hash table
    FunctionStats *functions;
    int function_count;
    int function_capacity;
    int *function_buckets;      // Hash buckets (-1 if empty)

    // Allocation tracking
    AllocSite *alloc_sites;
    int alloc_site_count;
    int alloc_site_capacity;
    int *alloc_buckets;

    // Overall stats
    uint64_t start_time_ns;
    uint64_t total_time_ns;
    uint64_t total_alloc_bytes;
    uint64_t total_alloc_count;

    // Current call stack for flamegraph
    ProfileStack call_stack;

    // Flamegraph samples (collapsed format strings)
    char **flamegraph_samples;
    uint64_t *flamegraph_counts;
    int flamegraph_sample_count;
    int flamegraph_sample_capacity;

    // Active timing frame stack (for self-time calculation)
    struct {
        int function_idx;
        uint64_t entry_time_ns;
        uint64_t child_time_ns;  // Time spent in child calls
    } *timing_stack;
    int timing_stack_depth;
    int timing_stack_capacity;

};

// ========== PROFILER API ==========

// Lifecycle
ProfilerState* profiler_new(ProfileMode mode);
void profiler_free(ProfilerState *state);
void profiler_start(ProfilerState *state);
void profiler_stop(ProfilerState *state);

// Instrumentation hooks (called by interpreter)
void profiler_enter_function(ProfilerState *state, const char *name,
                             const char *source_file, int line);
void profiler_exit_function(ProfilerState *state);
void profiler_record_alloc(ProfilerState *state, const char *source_file,
                           int line, uint64_t bytes);
void profiler_record_free(ProfilerState *state, const char *source_file,
                          int line, uint64_t bytes);

// Output
void profiler_print_report(ProfilerState *state, FILE *output);
void profiler_print_json(ProfilerState *state, FILE *output);
void profiler_print_flamegraph(ProfilerState *state, FILE *output);

// Utility
uint64_t profiler_get_time_ns(void);

#endif // HEMLOCK_PROFILER_H
