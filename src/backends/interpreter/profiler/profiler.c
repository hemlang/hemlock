/*
 * Hemlock Profiler Implementation
 *
 * Provides CPU time profiling, memory tracking, and call graph generation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "profiler.h"
#include "hemlock_limits.h"

// ========== TIME UTILITIES ==========

uint64_t profiler_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * HML_NANOSECONDS_PER_SECOND + ts.tv_nsec;
}

// ========== HASH UTILITIES ==========

static uint32_t hash_string(const char *str) {
    uint32_t hash = HML_DJB2_HASH_SEED;
    while (*str) {
        hash = ((hash << 5) + hash) + (unsigned char)*str++;
    }
    return hash;
}

static uint32_t hash_location(const char *file, int line) {
    uint32_t hash = hash_string(file ? file : "<unknown>");
    hash = ((hash << 5) + hash) + (uint32_t)line;
    return hash;
}

// ========== PROFILER LIFECYCLE ==========

ProfilerState* profiler_new(ProfileMode mode) {
    ProfilerState *state = calloc(1, sizeof(ProfilerState));
    if (!state) {
        fprintf(stderr, "Fatal: Failed to allocate profiler state\n");
        exit(1);
    }

    state->enabled = false;
    state->mode = mode;
    state->output_format = PROFILE_OUTPUT_TEXT;
    state->top_n = 20;

    // Initialize function stats
    state->function_capacity = 256;
    state->function_count = 0;
    state->functions = calloc(state->function_capacity, sizeof(FunctionStats));
    state->function_buckets = malloc(HML_PROFILER_HASH_SIZE * sizeof(int));
    for (int i = 0; i < HML_PROFILER_HASH_SIZE; i++) {
        state->function_buckets[i] = -1;
    }

    // Initialize allocation tracking
    state->alloc_site_capacity = 128;
    state->alloc_site_count = 0;
    state->alloc_sites = calloc(state->alloc_site_capacity, sizeof(AllocSite));
    state->alloc_buckets = malloc(HML_PROFILER_HASH_SIZE * sizeof(int));
    for (int i = 0; i < HML_PROFILER_HASH_SIZE; i++) {
        state->alloc_buckets[i] = -1;
    }

    // Initialize call stack for flamegraph
    state->call_stack.capacity = 64;
    state->call_stack.depth = 0;
    state->call_stack.stack_indices = malloc(state->call_stack.capacity * sizeof(int));

    // Initialize timing stack
    state->timing_stack_capacity = 64;
    state->timing_stack_depth = 0;
    state->timing_stack = malloc(state->timing_stack_capacity * sizeof(*state->timing_stack));

    // Initialize flamegraph samples
    state->flamegraph_sample_capacity = 256;
    state->flamegraph_sample_count = 0;
    state->flamegraph_samples = malloc(state->flamegraph_sample_capacity * sizeof(char*));
    state->flamegraph_counts = malloc(state->flamegraph_sample_capacity * sizeof(uint64_t));

    return state;
}

void profiler_free(ProfilerState *state) {
    if (!state) return;

    free(state->functions);
    free(state->function_buckets);
    free(state->alloc_sites);
    free(state->alloc_buckets);
    free(state->call_stack.stack_indices);
    free(state->timing_stack);

    // Free flamegraph samples
    for (int i = 0; i < state->flamegraph_sample_count; i++) {
        free(state->flamegraph_samples[i]);
    }
    free(state->flamegraph_samples);
    free(state->flamegraph_counts);

    free(state);
}

void profiler_start(ProfilerState *state) {
    if (!state) return;
    state->enabled = true;
    state->start_time_ns = profiler_get_time_ns();
}

void profiler_stop(ProfilerState *state) {
    if (!state) return;
    state->total_time_ns = profiler_get_time_ns() - state->start_time_ns;
    state->enabled = false;
}

// ========== FUNCTION TRACKING ==========

// Find or create a function stats entry
static int find_or_create_function(ProfilerState *state, const char *name,
                                   const char *source_file, int line) {
    uint32_t hash = hash_string(name);
    int bucket = hash % HML_PROFILER_HASH_SIZE;

    // Search existing entries in the bucket
    int idx = state->function_buckets[bucket];
    while (idx >= 0) {
        FunctionStats *fn = &state->functions[idx];
        if (fn->hash == hash && strcmp(fn->name, name) == 0) {
            return idx;
        }
        idx = fn->next;
    }

    // Not found - create new entry
    if (state->function_count >= state->function_capacity) {
        state->function_capacity *= 2;
        state->functions = realloc(state->functions,
                                   state->function_capacity * sizeof(FunctionStats));
        if (!state->functions) {
            fprintf(stderr, "Fatal: Failed to grow profiler function table\n");
            exit(1);
        }
    }

    idx = state->function_count++;
    FunctionStats *fn = &state->functions[idx];
    fn->name = name;
    fn->source_file = source_file;
    fn->line = line;
    fn->hash = hash;
    fn->total_time_ns = 0;
    fn->self_time_ns = 0;
    fn->call_count = 0;
    fn->max_time_ns = 0;
    fn->min_time_ns = UINT64_MAX;
    fn->alloc_bytes = 0;
    fn->alloc_count = 0;

    // Insert at head of bucket chain
    fn->next = state->function_buckets[bucket];
    state->function_buckets[bucket] = idx;

    return idx;
}

void profiler_enter_function(ProfilerState *state, const char *name,
                             const char *source_file, int line) {
    if (!state || !state->enabled) return;

    int fn_idx = find_or_create_function(state, name, source_file, line);
    FunctionStats *fn = &state->functions[fn_idx];
    fn->call_count++;

    uint64_t now = profiler_get_time_ns();

    // Push onto timing stack
    if (state->timing_stack_depth >= state->timing_stack_capacity) {
        state->timing_stack_capacity *= 2;
        state->timing_stack = realloc(state->timing_stack,
                                      state->timing_stack_capacity * sizeof(*state->timing_stack));
    }
    state->timing_stack[state->timing_stack_depth].function_idx = fn_idx;
    state->timing_stack[state->timing_stack_depth].entry_time_ns = now;
    state->timing_stack[state->timing_stack_depth].child_time_ns = 0;
    state->timing_stack_depth++;

    // Push onto call stack for flamegraph
    if (state->call_stack.depth >= state->call_stack.capacity) {
        state->call_stack.capacity *= 2;
        state->call_stack.stack_indices = realloc(state->call_stack.stack_indices,
                                                   state->call_stack.capacity * sizeof(int));
    }
    state->call_stack.stack_indices[state->call_stack.depth++] = fn_idx;
}

void profiler_exit_function(ProfilerState *state) {
    if (!state || !state->enabled || state->timing_stack_depth == 0) return;

    uint64_t now = profiler_get_time_ns();

    // Pop timing stack
    state->timing_stack_depth--;
    int fn_idx = state->timing_stack[state->timing_stack_depth].function_idx;
    uint64_t entry_time = state->timing_stack[state->timing_stack_depth].entry_time_ns;
    uint64_t child_time = state->timing_stack[state->timing_stack_depth].child_time_ns;

    uint64_t total_time = now - entry_time;
    uint64_t self_time = total_time - child_time;

    FunctionStats *fn = &state->functions[fn_idx];
    fn->total_time_ns += total_time;
    fn->self_time_ns += self_time;

    if (total_time > fn->max_time_ns) fn->max_time_ns = total_time;
    if (total_time < fn->min_time_ns) fn->min_time_ns = total_time;

    // Add our total time to parent's child time
    if (state->timing_stack_depth > 0) {
        state->timing_stack[state->timing_stack_depth - 1].child_time_ns += total_time;
    }

    // Pop call stack
    if (state->call_stack.depth > 0) {
        state->call_stack.depth--;
    }
}

// ========== ALLOCATION TRACKING ==========

static int find_or_create_alloc_site(ProfilerState *state, const char *source_file, int line) {
    uint32_t hash = hash_location(source_file, line);
    int bucket = hash % HML_PROFILER_HASH_SIZE;

    // Search existing entries
    int idx = state->alloc_buckets[bucket];
    while (idx >= 0) {
        AllocSite *site = &state->alloc_sites[idx];
        if (site->hash == hash && site->line == line &&
            ((site->source_file == source_file) ||
             (site->source_file && source_file && strcmp(site->source_file, source_file) == 0))) {
            return idx;
        }
        idx = site->next;
    }

    // Create new entry
    if (state->alloc_site_count >= state->alloc_site_capacity) {
        state->alloc_site_capacity *= 2;
        state->alloc_sites = realloc(state->alloc_sites,
                                      state->alloc_site_capacity * sizeof(AllocSite));
    }

    idx = state->alloc_site_count++;
    AllocSite *site = &state->alloc_sites[idx];
    site->source_file = source_file;
    site->line = line;
    site->hash = hash;
    site->total_bytes = 0;
    site->alloc_count = 0;
    site->current_bytes = 0;
    site->max_bytes = 0;
    site->next = state->alloc_buckets[bucket];
    state->alloc_buckets[bucket] = idx;

    return idx;
}

void profiler_record_alloc(ProfilerState *state, const char *source_file,
                           int line, uint64_t bytes) {
    if (!state || !state->enabled) return;

    state->total_alloc_bytes += bytes;
    state->total_alloc_count++;

    int site_idx = find_or_create_alloc_site(state, source_file, line);
    AllocSite *site = &state->alloc_sites[site_idx];
    site->total_bytes += bytes;
    site->alloc_count++;
    site->current_bytes += bytes;
    if (site->current_bytes > site->max_bytes) {
        site->max_bytes = site->current_bytes;
    }

    // Also track allocation in current function
    if (state->timing_stack_depth > 0) {
        int fn_idx = state->timing_stack[state->timing_stack_depth - 1].function_idx;
        FunctionStats *fn = &state->functions[fn_idx];
        fn->alloc_bytes += bytes;
        fn->alloc_count++;
    }
}

void profiler_record_free(ProfilerState *state, const char *source_file,
                          int line, uint64_t bytes) {
    if (!state || !state->enabled) return;

    // Find the allocation site and update current bytes
    int site_idx = find_or_create_alloc_site(state, source_file, line);
    AllocSite *site = &state->alloc_sites[site_idx];
    if (site->current_bytes >= bytes) {
        site->current_bytes -= bytes;
    }
}

// ========== COMPARISON FUNCTIONS FOR SORTING ==========

static int compare_by_self_time(const void *a, const void *b) {
    const FunctionStats *fa = *(const FunctionStats **)a;
    const FunctionStats *fb = *(const FunctionStats **)b;
    if (fb->self_time_ns > fa->self_time_ns) return 1;
    if (fb->self_time_ns < fa->self_time_ns) return -1;
    return 0;
}

static int compare_by_total_time(const void *a, const void *b) {
    const FunctionStats *fa = *(const FunctionStats **)a;
    const FunctionStats *fb = *(const FunctionStats **)b;
    if (fb->total_time_ns > fa->total_time_ns) return 1;
    if (fb->total_time_ns < fa->total_time_ns) return -1;
    return 0;
}

static int compare_by_alloc_bytes(const void *a, const void *b) {
    const AllocSite *sa = *(const AllocSite **)a;
    const AllocSite *sb = *(const AllocSite **)b;
    if (sb->total_bytes > sa->total_bytes) return 1;
    if (sb->total_bytes < sa->total_bytes) return -1;
    return 0;
}

// ========== OUTPUT: TEXT FORMAT ==========

static void format_time(uint64_t ns, char *buf, size_t size) {
    if (ns >= 1000000000ULL) {
        snprintf(buf, size, "%.3fs", (double)ns / 1000000000.0);
    } else if (ns >= 1000000ULL) {
        snprintf(buf, size, "%.3fms", (double)ns / 1000000.0);
    } else if (ns >= 1000ULL) {
        snprintf(buf, size, "%.3fus", (double)ns / 1000.0);
    } else {
        snprintf(buf, size, "%luns", (unsigned long)ns);
    }
}

static void format_bytes(uint64_t bytes, char *buf, size_t size) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        snprintf(buf, size, "%.2fGB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024) {
        snprintf(buf, size, "%.2fMB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(buf, size, "%.2fKB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, size, "%luB", (unsigned long)bytes);
    }
}

void profiler_print_report(ProfilerState *state, FILE *output) {
    if (!state || state->function_count == 0) {
        fprintf(output, "No profiling data collected.\n");
        return;
    }

    char time_buf[32];
    format_time(state->total_time_ns, time_buf, sizeof(time_buf));
    fprintf(output, "\n");
    fprintf(output, "=== Hemlock Profiler Report ===\n");
    fprintf(output, "\n");
    fprintf(output, "Total time: %s\n", time_buf);
    fprintf(output, "Functions called: %d unique\n", state->function_count);

    if (state->mode == PROFILE_MODE_MEMORY || state->total_alloc_bytes > 0) {
        char bytes_buf[32];
        format_bytes(state->total_alloc_bytes, bytes_buf, sizeof(bytes_buf));
        fprintf(output, "Total allocations: %lu (%s)\n",
                (unsigned long)state->total_alloc_count, bytes_buf);
    }

    // Sort functions by self time
    FunctionStats **sorted = malloc(state->function_count * sizeof(FunctionStats*));
    for (int i = 0; i < state->function_count; i++) {
        sorted[i] = &state->functions[i];
    }
    qsort(sorted, state->function_count, sizeof(FunctionStats*), compare_by_self_time);

    int show_count = (state->top_n > 0 && state->top_n < state->function_count)
                     ? state->top_n : state->function_count;

    fprintf(output, "\n");
    fprintf(output, "--- Top %d by Self Time ---\n", show_count);
    fprintf(output, "\n");
    fprintf(output, "%-30s %10s %10s %8s %10s\n",
            "Function", "Self", "Total", "Calls", "Avg");
    fprintf(output, "%-30s %10s %10s %8s %10s\n",
            "--------", "----", "-----", "-----", "---");

    for (int i = 0; i < show_count; i++) {
        FunctionStats *fn = sorted[i];
        if (fn->call_count == 0) continue;

        char self_buf[32], total_buf[32], avg_buf[32];
        format_time(fn->self_time_ns, self_buf, sizeof(self_buf));
        format_time(fn->total_time_ns, total_buf, sizeof(total_buf));
        format_time(fn->total_time_ns / fn->call_count, avg_buf, sizeof(avg_buf));

        // Truncate function name if too long
        char name_buf[31];
        if (strlen(fn->name) > 30) {
            snprintf(name_buf, sizeof(name_buf), "%.27s...", fn->name);
        } else {
            snprintf(name_buf, sizeof(name_buf), "%s", fn->name);
        }

        double self_pct = (state->total_time_ns > 0)
                          ? (100.0 * fn->self_time_ns / state->total_time_ns) : 0;

        fprintf(output, "%-30s %9s %10s %8lu %10s  (%.1f%%)\n",
                name_buf, self_buf, total_buf,
                (unsigned long)fn->call_count, avg_buf, self_pct);
    }

    // Show memory stats if available
    if (state->mode == PROFILE_MODE_MEMORY && state->alloc_site_count > 0) {
        AllocSite **sorted_sites = malloc(state->alloc_site_count * sizeof(AllocSite*));
        for (int i = 0; i < state->alloc_site_count; i++) {
            sorted_sites[i] = &state->alloc_sites[i];
        }
        qsort(sorted_sites, state->alloc_site_count, sizeof(AllocSite*), compare_by_alloc_bytes);

        int site_count = (state->top_n > 0 && state->top_n < state->alloc_site_count)
                         ? state->top_n : state->alloc_site_count;

        fprintf(output, "\n");
        fprintf(output, "--- Top %d Allocation Sites ---\n", site_count);
        fprintf(output, "\n");
        fprintf(output, "%-40s %10s %8s\n", "Location", "Total", "Count");
        fprintf(output, "%-40s %10s %8s\n", "--------", "-----", "-----");

        for (int i = 0; i < site_count; i++) {
            AllocSite *site = sorted_sites[i];
            char bytes_buf[32];
            format_bytes(site->total_bytes, bytes_buf, sizeof(bytes_buf));

            char loc_buf[41];
            if (site->source_file) {
                snprintf(loc_buf, sizeof(loc_buf), "%s:%d", site->source_file, site->line);
            } else {
                snprintf(loc_buf, sizeof(loc_buf), "<unknown>:%d", site->line);
            }

            fprintf(output, "%-40s %10s %8lu\n",
                    loc_buf, bytes_buf, (unsigned long)site->alloc_count);
        }
        free(sorted_sites);
    }

    free(sorted);
    fprintf(output, "\n");
}

// ========== OUTPUT: JSON FORMAT ==========

static void json_escape_string(const char *str, FILE *output) {
    fputc('"', output);
    if (str) {
        for (const char *p = str; *p; p++) {
            switch (*p) {
                case '"':  fputs("\\\"", output); break;
                case '\\': fputs("\\\\", output); break;
                case '\n': fputs("\\n", output); break;
                case '\r': fputs("\\r", output); break;
                case '\t': fputs("\\t", output); break;
                default:   fputc(*p, output); break;
            }
        }
    }
    fputc('"', output);
}

void profiler_print_json(ProfilerState *state, FILE *output) {
    if (!state) {
        fprintf(output, "{}\n");
        return;
    }

    fprintf(output, "{\n");
    fprintf(output, "  \"total_time_ns\": %lu,\n", (unsigned long)state->total_time_ns);
    fprintf(output, "  \"function_count\": %d,\n", state->function_count);
    fprintf(output, "  \"total_alloc_bytes\": %lu,\n", (unsigned long)state->total_alloc_bytes);
    fprintf(output, "  \"total_alloc_count\": %lu,\n", (unsigned long)state->total_alloc_count);

    fprintf(output, "  \"functions\": [\n");
    for (int i = 0; i < state->function_count; i++) {
        FunctionStats *fn = &state->functions[i];
        fprintf(output, "    {\n");
        fprintf(output, "      \"name\": ");
        json_escape_string(fn->name, output);
        fprintf(output, ",\n");
        fprintf(output, "      \"source_file\": ");
        json_escape_string(fn->source_file, output);
        fprintf(output, ",\n");
        fprintf(output, "      \"line\": %d,\n", fn->line);
        fprintf(output, "      \"call_count\": %lu,\n", (unsigned long)fn->call_count);
        fprintf(output, "      \"total_time_ns\": %lu,\n", (unsigned long)fn->total_time_ns);
        fprintf(output, "      \"self_time_ns\": %lu,\n", (unsigned long)fn->self_time_ns);
        fprintf(output, "      \"max_time_ns\": %lu,\n", (unsigned long)fn->max_time_ns);
        fprintf(output, "      \"min_time_ns\": %lu,\n",
                fn->min_time_ns == UINT64_MAX ? 0UL : (unsigned long)fn->min_time_ns);
        fprintf(output, "      \"alloc_bytes\": %lu,\n", (unsigned long)fn->alloc_bytes);
        fprintf(output, "      \"alloc_count\": %lu\n", (unsigned long)fn->alloc_count);
        fprintf(output, "    }%s\n", (i < state->function_count - 1) ? "," : "");
    }
    fprintf(output, "  ],\n");

    fprintf(output, "  \"alloc_sites\": [\n");
    for (int i = 0; i < state->alloc_site_count; i++) {
        AllocSite *site = &state->alloc_sites[i];
        fprintf(output, "    {\n");
        fprintf(output, "      \"source_file\": ");
        json_escape_string(site->source_file, output);
        fprintf(output, ",\n");
        fprintf(output, "      \"line\": %d,\n", site->line);
        fprintf(output, "      \"total_bytes\": %lu,\n", (unsigned long)site->total_bytes);
        fprintf(output, "      \"alloc_count\": %lu,\n", (unsigned long)site->alloc_count);
        fprintf(output, "      \"current_bytes\": %lu,\n", (unsigned long)site->current_bytes);
        fprintf(output, "      \"max_bytes\": %lu\n", (unsigned long)site->max_bytes);
        fprintf(output, "    }%s\n", (i < state->alloc_site_count - 1) ? "," : "");
    }
    fprintf(output, "  ]\n");

    fprintf(output, "}\n");
}

// ========== OUTPUT: FLAMEGRAPH FORMAT ==========

void profiler_print_flamegraph(ProfilerState *state, FILE *output) {
    if (!state) return;

    // For flamegraph output, we need to track call stacks during execution
    // and aggregate them. Since we track function entries/exits, we build
    // a simpler flat profile.
    //
    // Flamegraph format: stack;frames;separated;by;semicolons count
    // For now, we output function self-times as single-frame stacks.
    // A proper flamegraph would require stack sampling during execution.

    // Sort by self time
    FunctionStats **sorted = malloc(state->function_count * sizeof(FunctionStats*));
    for (int i = 0; i < state->function_count; i++) {
        sorted[i] = &state->functions[i];
    }
    qsort(sorted, state->function_count, sizeof(FunctionStats*), compare_by_self_time);

    // Output in collapsed flamegraph format
    // Scale to microseconds for better visualization
    for (int i = 0; i < state->function_count; i++) {
        FunctionStats *fn = sorted[i];
        if (fn->self_time_ns == 0) continue;

        // Convert ns to us for flamegraph (otherwise numbers are too large)
        uint64_t self_us = fn->self_time_ns / 1000;
        if (self_us == 0) self_us = 1;  // At least 1 for visibility

        fprintf(output, "%s %lu\n", fn->name, (unsigned long)self_us);
    }

    free(sorted);
}
