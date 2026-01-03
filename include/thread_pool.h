/*
 * Hemlock Thread Pool with Work-Stealing Scheduler
 *
 * A fixed-size thread pool where workers can steal work from each other
 * to balance load. Uses Chase-Lev work-stealing deques for efficient
 * lock-free operations on the local end.
 *
 * Design:
 * - Each worker has a local deque (double-ended queue)
 * - Workers push/pop from bottom of their own deque (LIFO - cache locality)
 * - Workers steal from top of other workers' deques (FIFO - oldest tasks)
 * - External submissions go to a global queue with condition variable
 * - Workers check local deque, then global queue, then steal from others
 */

#ifndef HEMLOCK_THREAD_POOL_H
#define HEMLOCK_THREAD_POOL_H

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include "hemlock_limits.h"

// Forward declarations
typedef struct ThreadPool ThreadPool;
typedef struct Worker Worker;
typedef struct WorkStealingDeque WorkStealingDeque;
typedef struct WorkItem WorkItem;

// Work item callback type
// The callback receives the work item data and returns a result
typedef void* (*WorkItemFunc)(void* data, void* ctx);

// Work item - a unit of work to execute
typedef struct WorkItem {
    WorkItemFunc func;          // Function to execute
    void* data;                 // Data to pass to function
    void* ctx;                  // Context (ExecutionContext for interpreter)
    void* result;               // Result storage (set by callback)
    atomic_int completed;       // 1 when work is done
    pthread_mutex_t* mutex;     // For waiting on completion
    pthread_cond_t* cond;       // For signaling completion
    int has_waiter;             // 1 if someone is waiting for result
    struct WorkItem* next;      // For linked list (global queue)
} WorkItem;

// Chase-Lev work-stealing deque
// Lock-free for push/pop (owner), uses CAS for steal
typedef struct WorkStealingDeque {
    WorkItem** items;           // Circular buffer of work items
    atomic_long bottom;         // Bottom index (owner pushes/pops here)
    atomic_long top;            // Top index (thieves steal from here)
    int capacity;               // Current capacity
    int max_capacity;           // Maximum allowed capacity
    pthread_mutex_t resize_lock; // Lock for resizing only
} WorkStealingDeque;

// Per-worker state
typedef struct Worker {
    int id;                     // Worker ID (0 to num_workers-1)
    pthread_t thread;           // Worker thread
    ThreadPool* pool;           // Back-pointer to pool
    WorkStealingDeque* deque;   // Local work-stealing deque
    unsigned int steal_seed;    // Random seed for choosing steal victim
    atomic_int active;          // 1 if actively working, 0 if idle
    atomic_long tasks_executed; // Statistics: number of tasks executed
    atomic_long tasks_stolen;   // Statistics: number of tasks stolen
} Worker;

// Global submission queue (MPSC - multiple producers, single consumers)
typedef struct SubmissionQueue {
    WorkItem* head;             // Head of linked list
    WorkItem* tail;             // Tail of linked list
    int count;                  // Number of items in queue
    int capacity;               // Maximum capacity
    pthread_mutex_t mutex;      // Protects queue access
    pthread_cond_t not_empty;   // Signal when items are available
} SubmissionQueue;

// Thread pool statistics
typedef struct ThreadPoolStats {
    long total_tasks_submitted; // Total tasks submitted to pool
    long total_tasks_completed; // Total tasks completed
    long total_steals;          // Total successful steals
    long total_steal_attempts;  // Total steal attempts
} ThreadPoolStats;

// The thread pool
typedef struct ThreadPool {
    int num_workers;            // Number of worker threads
    Worker* workers;            // Array of workers
    SubmissionQueue* submission; // Global submission queue
    atomic_int shutdown;        // 1 when shutting down
    atomic_int started;         // 1 when workers have started
    pthread_mutex_t start_mutex; // For coordinating startup
    pthread_cond_t start_cond;  // For signaling startup complete
    ThreadPoolStats stats;      // Pool statistics
} ThreadPool;

// ========== THREAD POOL API ==========

// Initialize the global thread pool
// num_workers: number of worker threads (0 = auto-detect based on CPU count)
// Returns: 0 on success, -1 on error
int thread_pool_init(int num_workers);

// Shutdown the global thread pool
// Waits for all pending work to complete
void thread_pool_shutdown(void);

// Submit a work item to the thread pool
// If called from a worker thread, pushes to local deque
// Otherwise, pushes to global submission queue
// func: function to execute
// data: data to pass to function
// ctx: execution context
// Returns: WorkItem pointer (can be used to wait for result)
WorkItem* thread_pool_submit(WorkItemFunc func, void* data, void* ctx);

// Submit work and wait for result (blocking)
// Returns: result from work item function
void* thread_pool_submit_wait(WorkItemFunc func, void* data, void* ctx);

// Wait for a work item to complete
// Returns: result from work item function
void* work_item_wait(WorkItem* item);

// Free a work item (must be called after waiting or if not waiting)
void work_item_free(WorkItem* item);

// Check if current thread is a pool worker
// Returns: worker ID (0 to num_workers-1) or -1 if not a worker
int thread_pool_current_worker_id(void);

// Get thread pool statistics
ThreadPoolStats thread_pool_get_stats(void);

// Check if thread pool is initialized
int thread_pool_is_initialized(void);

// Get the global thread pool (for internal use)
ThreadPool* thread_pool_get(void);

// ========== WORK-STEALING DEQUE API (internal) ==========

// Create a new work-stealing deque
WorkStealingDeque* deque_new(int initial_capacity);

// Free a work-stealing deque
void deque_free(WorkStealingDeque* deque);

// Push a work item to the bottom of the deque (owner only)
// Returns: 0 on success, -1 if full
int deque_push(WorkStealingDeque* deque, WorkItem* item);

// Pop a work item from the bottom of the deque (owner only)
// Returns: work item or NULL if empty
WorkItem* deque_pop(WorkStealingDeque* deque);

// Steal a work item from the top of the deque (any thread)
// Returns: work item or NULL if empty
WorkItem* deque_steal(WorkStealingDeque* deque);

// Check if deque is empty
int deque_is_empty(WorkStealingDeque* deque);

// Get number of items in deque (approximate)
long deque_size(WorkStealingDeque* deque);

#endif // HEMLOCK_THREAD_POOL_H
