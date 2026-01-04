/*
 * Hemlock Runtime Thread Pool with Work-Stealing Scheduler
 *
 * A fixed-size thread pool where workers can steal work from each other
 * to balance load. Uses Chase-Lev work-stealing deques for efficient
 * lock-free operations on the local end.
 */

#ifndef HEMLOCK_RUNTIME_THREAD_POOL_H
#define HEMLOCK_RUNTIME_THREAD_POOL_H

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

// Thread pool configuration constants
#define HML_THREADPOOL_DEFAULT_WORKERS 0        // 0 = auto-detect
#define HML_THREADPOOL_MAX_WORKERS 256
#define HML_THREADPOOL_MIN_WORKERS 2
#define HML_THREADPOOL_DEQUE_INITIAL_CAPACITY 64
#define HML_THREADPOOL_DEQUE_MAX_CAPACITY 65536
#define HML_THREADPOOL_SUBMISSION_QUEUE_CAPACITY 4096
#define HML_THREADPOOL_STEAL_ATTEMPTS 32
#define HML_THREADPOOL_IDLE_SLEEP_US 100
#define HML_THREADPOOL_STEAL_SEED_MULT 1103515245

// Forward declarations
typedef struct HmlThreadPool HmlThreadPool;
typedef struct HmlWorker HmlWorker;
typedef struct HmlWorkStealingDeque HmlWorkStealingDeque;
typedef struct HmlWorkItem HmlWorkItem;

// Work item callback type
typedef void* (*HmlWorkItemFunc)(void* data, void* ctx);

// Work item - a unit of work to execute
typedef struct HmlWorkItem {
    HmlWorkItemFunc func;       // Function to execute
    void* data;                 // Data to pass to function
    void* ctx;                  // Context
    void* result;               // Result storage
    atomic_int completed;       // 1 when work is done
    pthread_mutex_t* mutex;     // For waiting on completion
    pthread_cond_t* cond;       // For signaling completion
    int has_waiter;             // 1 if someone is waiting for result
    struct HmlWorkItem* next;   // For linked list
} HmlWorkItem;

// Chase-Lev work-stealing deque
typedef struct HmlWorkStealingDeque {
    HmlWorkItem** items;
    atomic_long bottom;
    atomic_long top;
    int capacity;
    int max_capacity;
    pthread_mutex_t resize_lock;
} HmlWorkStealingDeque;

// Per-worker state
typedef struct HmlWorker {
    int id;
    pthread_t thread;
    HmlThreadPool* pool;
    HmlWorkStealingDeque* deque;
    unsigned int steal_seed;
    atomic_int active;
    atomic_long tasks_executed;
    atomic_long tasks_stolen;
} HmlWorker;

// Global submission queue
typedef struct HmlSubmissionQueue {
    HmlWorkItem* head;
    HmlWorkItem* tail;
    int count;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
} HmlSubmissionQueue;

// The thread pool
typedef struct HmlThreadPool {
    int num_workers;
    HmlWorker* workers;
    HmlSubmissionQueue* submission;
    atomic_int shutdown;
    atomic_int started;
    pthread_mutex_t start_mutex;
    pthread_cond_t start_cond;
} HmlThreadPool;

// ========== THREAD POOL API ==========

// Initialize the global thread pool
int hml_thread_pool_init(int num_workers);

// Shutdown the global thread pool
void hml_thread_pool_shutdown(void);

// Submit a work item to the thread pool
HmlWorkItem* hml_thread_pool_submit(HmlWorkItemFunc func, void* data, void* ctx);

// Wait for a work item to complete
void* hml_work_item_wait(HmlWorkItem* item);

// Free a work item
void hml_work_item_free(HmlWorkItem* item);

// Check if current thread is a pool worker
int hml_thread_pool_current_worker_id(void);

// Check if thread pool is initialized
int hml_thread_pool_is_initialized(void);

#endif // HEMLOCK_RUNTIME_THREAD_POOL_H
