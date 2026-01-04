/*
 * Hemlock Thread Pool with Work-Stealing Scheduler
 *
 * Implementation of a work-stealing thread pool for async task execution.
 * Workers steal from each other when idle to balance load automatically.
 */

#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// Thread-local storage for current worker
static __thread Worker* tls_current_worker = NULL;

// Global thread pool instance
static ThreadPool* g_thread_pool = NULL;
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

// ========== WORK-STEALING DEQUE IMPLEMENTATION ==========
// Based on the Chase-Lev work-stealing deque algorithm

WorkStealingDeque* deque_new(int initial_capacity) {
    WorkStealingDeque* deque = malloc(sizeof(WorkStealingDeque));
    if (!deque) return NULL;

    deque->items = calloc(initial_capacity, sizeof(WorkItem*));
    if (!deque->items) {
        free(deque);
        return NULL;
    }

    atomic_init(&deque->bottom, 0);
    atomic_init(&deque->top, 0);
    deque->capacity = initial_capacity;
    deque->max_capacity = HML_THREADPOOL_DEQUE_MAX_CAPACITY;
    pthread_mutex_init(&deque->resize_lock, NULL);

    return deque;
}

void deque_free(WorkStealingDeque* deque) {
    if (!deque) return;
    pthread_mutex_destroy(&deque->resize_lock);
    free(deque->items);
    free(deque);
}

// Resize the deque (double capacity)
static int deque_resize(WorkStealingDeque* deque) {
    pthread_mutex_lock(&deque->resize_lock);

    int old_capacity = deque->capacity;
    int new_capacity = old_capacity * 2;

    if (new_capacity > deque->max_capacity) {
        pthread_mutex_unlock(&deque->resize_lock);
        return -1; // Cannot grow further
    }

    WorkItem** new_items = calloc(new_capacity, sizeof(WorkItem*));
    if (!new_items) {
        pthread_mutex_unlock(&deque->resize_lock);
        return -1;
    }

    // Copy items to new buffer
    long top = atomic_load_explicit(&deque->top, memory_order_acquire);
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_acquire);

    for (long i = top; i < bottom; i++) {
        new_items[i % new_capacity] = deque->items[i % old_capacity];
    }

    WorkItem** old_items = deque->items;
    deque->items = new_items;
    deque->capacity = new_capacity;

    pthread_mutex_unlock(&deque->resize_lock);

    // Free old buffer (safe because we hold resize_lock during transition)
    free(old_items);

    return 0;
}

int deque_push(WorkStealingDeque* deque, WorkItem* item) {
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed);
    long top = atomic_load_explicit(&deque->top, memory_order_acquire);

    long size = bottom - top;
    if (size >= deque->capacity - 1) {
        // Need to resize
        if (deque_resize(deque) != 0) {
            return -1; // Deque is full and cannot grow
        }
    }

    deque->items[bottom % deque->capacity] = item;
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);

    return 0;
}

WorkItem* deque_pop(WorkStealingDeque* deque) {
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&deque->bottom, bottom, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);

    long top = atomic_load_explicit(&deque->top, memory_order_relaxed);

    if (top <= bottom) {
        // Non-empty
        WorkItem* item = deque->items[bottom % deque->capacity];

        if (top == bottom) {
            // Last item - need CAS to compete with stealers
            if (!atomic_compare_exchange_strong_explicit(
                    &deque->top, &top, top + 1,
                    memory_order_seq_cst, memory_order_relaxed)) {
                // Lost race to stealer
                item = NULL;
            }
            atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);
        }
        return item;
    } else {
        // Empty - restore bottom
        atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);
        return NULL;
    }
}

WorkItem* deque_steal(WorkStealingDeque* deque) {
    long top = atomic_load_explicit(&deque->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_acquire);

    if (top < bottom) {
        // Non-empty
        WorkItem* item = deque->items[top % deque->capacity];

        if (!atomic_compare_exchange_strong_explicit(
                &deque->top, &top, top + 1,
                memory_order_seq_cst, memory_order_relaxed)) {
            // Lost race - another thread stole or owner popped
            return NULL;
        }
        return item;
    }
    return NULL;
}

int deque_is_empty(WorkStealingDeque* deque) {
    long top = atomic_load_explicit(&deque->top, memory_order_acquire);
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_acquire);
    return top >= bottom;
}

long deque_size(WorkStealingDeque* deque) {
    long top = atomic_load_explicit(&deque->top, memory_order_acquire);
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_acquire);
    return bottom - top;
}

// ========== SUBMISSION QUEUE IMPLEMENTATION ==========

static SubmissionQueue* submission_queue_new(int capacity) {
    SubmissionQueue* queue = malloc(sizeof(SubmissionQueue));
    if (!queue) return NULL;

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->capacity = capacity;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);

    return queue;
}

static void submission_queue_free(SubmissionQueue* queue) {
    if (!queue) return;

    // Free any remaining items
    pthread_mutex_lock(&queue->mutex);
    WorkItem* item = queue->head;
    while (item) {
        WorkItem* next = item->next;
        work_item_free(item);
        item = next;
    }
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    free(queue);
}

static int submission_queue_push(SubmissionQueue* queue, WorkItem* item) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->count >= queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return -1; // Queue full
    }

    item->next = NULL;

    if (queue->tail) {
        queue->tail->next = item;
        queue->tail = item;
    } else {
        queue->head = queue->tail = item;
    }
    queue->count++;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

static WorkItem* submission_queue_pop(SubmissionQueue* queue) {
    pthread_mutex_lock(&queue->mutex);

    WorkItem* item = queue->head;
    if (item) {
        queue->head = item->next;
        if (!queue->head) {
            queue->tail = NULL;
        }
        queue->count--;
        item->next = NULL;
    }

    pthread_mutex_unlock(&queue->mutex);
    return item;
}

// Wait for an item with timeout (returns NULL if timeout or shutdown)
static WorkItem* submission_queue_pop_wait(SubmissionQueue* queue, int timeout_us) {
    pthread_mutex_lock(&queue->mutex);

    if (!queue->head) {
        // Wait with timeout
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += timeout_us * 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &ts);
    }

    WorkItem* item = queue->head;
    if (item) {
        queue->head = item->next;
        if (!queue->head) {
            queue->tail = NULL;
        }
        queue->count--;
        item->next = NULL;
    }

    pthread_mutex_unlock(&queue->mutex);
    return item;
}

// ========== WORK ITEM IMPLEMENTATION ==========

static WorkItem* work_item_new(WorkItemFunc func, void* data, void* ctx) {
    WorkItem* item = malloc(sizeof(WorkItem));
    if (!item) return NULL;

    item->func = func;
    item->data = data;
    item->ctx = ctx;
    item->result = NULL;
    atomic_init(&item->completed, 0);
    item->mutex = NULL;
    item->cond = NULL;
    item->has_waiter = 0;
    item->next = NULL;

    return item;
}

void work_item_free(WorkItem* item) {
    if (!item) return;

    if (item->mutex) {
        pthread_mutex_destroy(item->mutex);
        free(item->mutex);
    }
    if (item->cond) {
        pthread_cond_destroy(item->cond);
        free(item->cond);
    }
    free(item);
}

static void work_item_setup_wait(WorkItem* item) {
    if (!item->mutex) {
        item->mutex = malloc(sizeof(pthread_mutex_t));
        item->cond = malloc(sizeof(pthread_cond_t));
        pthread_mutex_init(item->mutex, NULL);
        pthread_cond_init(item->cond, NULL);
    }
    item->has_waiter = 1;
}

static void work_item_signal_complete(WorkItem* item) {
    atomic_store_explicit(&item->completed, 1, memory_order_release);

    if (item->has_waiter && item->mutex && item->cond) {
        pthread_mutex_lock(item->mutex);
        pthread_cond_signal(item->cond);
        pthread_mutex_unlock(item->mutex);
    }
}

void* work_item_wait(WorkItem* item) {
    if (!item) return NULL;

    // Fast path: already completed
    if (atomic_load_explicit(&item->completed, memory_order_acquire)) {
        return item->result;
    }

    // Set up waiting
    work_item_setup_wait(item);

    pthread_mutex_lock(item->mutex);
    while (!atomic_load_explicit(&item->completed, memory_order_acquire)) {
        pthread_cond_wait(item->cond, item->mutex);
    }
    pthread_mutex_unlock(item->mutex);

    return item->result;
}

// ========== WORKER IMPLEMENTATION ==========

// Get a random victim worker for stealing (excluding self)
static int worker_random_victim(Worker* worker) {
    int num_workers = worker->pool->num_workers;
    if (num_workers <= 1) return -1;

    // Simple LCG random number generator
    worker->steal_seed = worker->steal_seed * HML_THREADPOOL_STEAL_SEED_MULT + 1;
    int victim = (worker->steal_seed >> 16) % num_workers;

    // Skip self
    if (victim == worker->id) {
        victim = (victim + 1) % num_workers;
    }

    return victim;
}

// Try to steal work from other workers
static WorkItem* worker_steal(Worker* worker) {
    ThreadPool* pool = worker->pool;

    for (int attempts = 0; attempts < HML_THREADPOOL_STEAL_ATTEMPTS; attempts++) {
        int victim_id = worker_random_victim(worker);
        if (victim_id < 0) break;

        Worker* victim = &pool->workers[victim_id];
        WorkItem* item = deque_steal(victim->deque);

        if (item) {
            atomic_fetch_add(&worker->tasks_stolen, 1);
            return item;
        }
    }

    return NULL;
}

// Get next work item for this worker
static WorkItem* worker_get_work(Worker* worker) {
    ThreadPool* pool = worker->pool;

    // 1. Check local deque first (LIFO - good cache locality)
    WorkItem* item = deque_pop(worker->deque);
    if (item) return item;

    // 2. Check global submission queue
    item = submission_queue_pop(pool->submission);
    if (item) return item;

    // 3. Try to steal from other workers
    item = worker_steal(worker);
    if (item) return item;

    return NULL;
}

// Execute a work item
static void worker_execute(Worker* worker, WorkItem* item) {
    atomic_store(&worker->active, 1);

    // Execute the work
    void* result = item->func(item->data, item->ctx);
    item->result = result;

    // Signal completion
    work_item_signal_complete(item);

    atomic_fetch_add(&worker->tasks_executed, 1);
    atomic_store(&worker->active, 0);
}

// Worker thread main loop
static void* worker_thread_main(void* arg) {
    Worker* worker = (Worker*)arg;
    ThreadPool* pool = worker->pool;

    // Set thread-local worker pointer
    tls_current_worker = worker;

    // Block all signals - only main thread should handle signals
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Signal that we're ready
    pthread_mutex_lock(&pool->start_mutex);
    pthread_cond_signal(&pool->start_cond);
    pthread_mutex_unlock(&pool->start_mutex);

    // Main work loop
    while (!atomic_load(&pool->shutdown)) {
        WorkItem* item = worker_get_work(worker);

        if (item) {
            worker_execute(worker, item);

            // If no waiter, free the item
            if (!item->has_waiter) {
                work_item_free(item);
            }
        } else {
            // No work available - wait on submission queue with timeout
            item = submission_queue_pop_wait(pool->submission,
                                             HML_THREADPOOL_IDLE_SLEEP_US);
            if (item) {
                worker_execute(worker, item);
                if (!item->has_waiter) {
                    work_item_free(item);
                }
            }
        }
    }

    // Drain remaining work before exiting
    WorkItem* item;
    while ((item = worker_get_work(worker)) != NULL) {
        worker_execute(worker, item);
        if (!item->has_waiter) {
            work_item_free(item);
        }
    }

    tls_current_worker = NULL;
    return NULL;
}

// ========== THREAD POOL IMPLEMENTATION ==========

static int get_cpu_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count <= 0) count = 4; // Default fallback
    return (int)count;
}

int thread_pool_init(int num_workers) {
    pthread_mutex_lock(&g_pool_mutex);

    if (g_thread_pool) {
        pthread_mutex_unlock(&g_pool_mutex);
        return 0; // Already initialized
    }

    // Determine number of workers
    // Use 2x CPU count to handle blocking tasks (channel waits, I/O)
    if (num_workers <= 0) {
        num_workers = get_cpu_count() * 2;
    }
    if (num_workers < HML_THREADPOOL_MIN_WORKERS) {
        num_workers = HML_THREADPOOL_MIN_WORKERS;
    }
    if (num_workers > HML_THREADPOOL_MAX_WORKERS) {
        num_workers = HML_THREADPOOL_MAX_WORKERS;
    }

    // Allocate pool
    ThreadPool* pool = malloc(sizeof(ThreadPool));
    if (!pool) {
        pthread_mutex_unlock(&g_pool_mutex);
        return -1;
    }

    pool->num_workers = num_workers;
    atomic_init(&pool->shutdown, 0);
    atomic_init(&pool->started, 0);
    pthread_mutex_init(&pool->start_mutex, NULL);
    pthread_cond_init(&pool->start_cond, NULL);
    memset(&pool->stats, 0, sizeof(ThreadPoolStats));

    // Create submission queue
    pool->submission = submission_queue_new(HML_THREADPOOL_SUBMISSION_QUEUE_CAPACITY);
    if (!pool->submission) {
        free(pool);
        pthread_mutex_unlock(&g_pool_mutex);
        return -1;
    }

    // Allocate workers
    pool->workers = calloc(num_workers, sizeof(Worker));
    if (!pool->workers) {
        submission_queue_free(pool->submission);
        free(pool);
        pthread_mutex_unlock(&g_pool_mutex);
        return -1;
    }

    // Initialize workers
    for (int i = 0; i < num_workers; i++) {
        Worker* worker = &pool->workers[i];
        worker->id = i;
        worker->pool = pool;
        worker->steal_seed = (unsigned int)(i * 1103515245 + 12345);
        atomic_init(&worker->active, 0);
        atomic_init(&worker->tasks_executed, 0);
        atomic_init(&worker->tasks_stolen, 0);

        worker->deque = deque_new(HML_THREADPOOL_DEQUE_INITIAL_CAPACITY);
        if (!worker->deque) {
            // Cleanup on error
            for (int j = 0; j < i; j++) {
                deque_free(pool->workers[j].deque);
            }
            free(pool->workers);
            submission_queue_free(pool->submission);
            free(pool);
            pthread_mutex_unlock(&g_pool_mutex);
            return -1;
        }
    }

    g_thread_pool = pool;

    // Start worker threads
    pthread_mutex_lock(&pool->start_mutex);

    for (int i = 0; i < num_workers; i++) {
        Worker* worker = &pool->workers[i];
        int rc = pthread_create(&worker->thread, NULL, worker_thread_main, worker);
        if (rc != 0) {
            fprintf(stderr, "Failed to create worker thread %d: %d\n", i, rc);
            // Continue with fewer workers
        }
    }

    // Wait for all workers to signal ready
    for (int i = 0; i < num_workers; i++) {
        pthread_cond_wait(&pool->start_cond, &pool->start_mutex);
    }

    atomic_store(&pool->started, 1);
    pthread_mutex_unlock(&pool->start_mutex);

    pthread_mutex_unlock(&g_pool_mutex);
    return 0;
}

void thread_pool_shutdown(void) {
    pthread_mutex_lock(&g_pool_mutex);

    if (!g_thread_pool) {
        pthread_mutex_unlock(&g_pool_mutex);
        return;
    }

    ThreadPool* pool = g_thread_pool;

    // Signal shutdown
    atomic_store(&pool->shutdown, 1);

    // Wake up all workers waiting on submission queue
    pthread_mutex_lock(&pool->submission->mutex);
    pthread_cond_broadcast(&pool->submission->not_empty);
    pthread_mutex_unlock(&pool->submission->mutex);

    // Wait for workers to exit
    for (int i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->workers[i].thread, NULL);
    }

    // Free workers
    for (int i = 0; i < pool->num_workers; i++) {
        deque_free(pool->workers[i].deque);
    }
    free(pool->workers);

    // Free submission queue
    submission_queue_free(pool->submission);

    // Free pool
    pthread_mutex_destroy(&pool->start_mutex);
    pthread_cond_destroy(&pool->start_cond);
    free(pool);

    g_thread_pool = NULL;

    pthread_mutex_unlock(&g_pool_mutex);
}

WorkItem* thread_pool_submit(WorkItemFunc func, void* data, void* ctx) {
    ThreadPool* pool = g_thread_pool;

    if (!pool) {
        // Auto-initialize with default workers
        if (thread_pool_init(HML_THREADPOOL_DEFAULT_WORKERS) != 0) {
            return NULL;
        }
        pool = g_thread_pool;
    }

    WorkItem* item = work_item_new(func, data, ctx);
    if (!item) return NULL;

    // If called from a worker thread, push to local deque
    if (tls_current_worker && tls_current_worker->pool == pool) {
        if (deque_push(tls_current_worker->deque, item) == 0) {
            return item;
        }
        // Fall through to submission queue if deque is full
    }

    // Push to global submission queue
    if (submission_queue_push(pool->submission, item) != 0) {
        work_item_free(item);
        return NULL;
    }

    return item;
}

void* thread_pool_submit_wait(WorkItemFunc func, void* data, void* ctx) {
    WorkItem* item = thread_pool_submit(func, data, ctx);
    if (!item) return NULL;

    void* result = work_item_wait(item);
    work_item_free(item);
    return result;
}

int thread_pool_current_worker_id(void) {
    if (tls_current_worker) {
        return tls_current_worker->id;
    }
    return -1;
}

ThreadPoolStats thread_pool_get_stats(void) {
    ThreadPoolStats stats = {0};
    ThreadPool* pool = g_thread_pool;

    if (!pool) return stats;

    for (int i = 0; i < pool->num_workers; i++) {
        stats.total_tasks_completed += atomic_load(&pool->workers[i].tasks_executed);
        stats.total_steals += atomic_load(&pool->workers[i].tasks_stolen);
    }

    return stats;
}

int thread_pool_is_initialized(void) {
    return g_thread_pool != NULL;
}

ThreadPool* thread_pool_get(void) {
    return g_thread_pool;
}
