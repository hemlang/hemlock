/*
 * Hemlock Runtime Thread Pool with Work-Stealing Scheduler
 *
 * Implementation of a work-stealing thread pool for async task execution.
 */

#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// Thread-local storage for current worker
static __thread HmlWorker* tls_current_worker = NULL;

// Global thread pool instance
static HmlThreadPool* g_thread_pool = NULL;
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

// ========== WORK-STEALING DEQUE IMPLEMENTATION ==========

static HmlWorkStealingDeque* deque_new(int initial_capacity) {
    HmlWorkStealingDeque* deque = malloc(sizeof(HmlWorkStealingDeque));
    if (!deque) return NULL;

    deque->items = calloc(initial_capacity, sizeof(HmlWorkItem*));
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

static void deque_free(HmlWorkStealingDeque* deque) {
    if (!deque) return;
    pthread_mutex_destroy(&deque->resize_lock);
    free(deque->items);
    free(deque);
}

static int deque_resize(HmlWorkStealingDeque* deque) {
    pthread_mutex_lock(&deque->resize_lock);

    int old_capacity = deque->capacity;
    int new_capacity = old_capacity * 2;

    if (new_capacity > deque->max_capacity) {
        pthread_mutex_unlock(&deque->resize_lock);
        return -1;
    }

    HmlWorkItem** new_items = calloc(new_capacity, sizeof(HmlWorkItem*));
    if (!new_items) {
        pthread_mutex_unlock(&deque->resize_lock);
        return -1;
    }

    long top = atomic_load_explicit(&deque->top, memory_order_acquire);
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_acquire);

    for (long i = top; i < bottom; i++) {
        new_items[i % new_capacity] = deque->items[i % old_capacity];
    }

    HmlWorkItem** old_items = deque->items;
    deque->items = new_items;
    deque->capacity = new_capacity;

    pthread_mutex_unlock(&deque->resize_lock);
    free(old_items);

    return 0;
}

static int deque_push(HmlWorkStealingDeque* deque, HmlWorkItem* item) {
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed);
    long top = atomic_load_explicit(&deque->top, memory_order_acquire);

    long size = bottom - top;
    if (size >= deque->capacity - 1) {
        if (deque_resize(deque) != 0) {
            return -1;
        }
    }

    deque->items[bottom % deque->capacity] = item;
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);

    return 0;
}

static HmlWorkItem* deque_pop(HmlWorkStealingDeque* deque) {
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&deque->bottom, bottom, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);

    long top = atomic_load_explicit(&deque->top, memory_order_relaxed);

    if (top <= bottom) {
        HmlWorkItem* item = deque->items[bottom % deque->capacity];

        if (top == bottom) {
            if (!atomic_compare_exchange_strong_explicit(
                    &deque->top, &top, top + 1,
                    memory_order_seq_cst, memory_order_relaxed)) {
                item = NULL;
            }
            atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);
        }
        return item;
    } else {
        atomic_store_explicit(&deque->bottom, bottom + 1, memory_order_relaxed);
        return NULL;
    }
}

static HmlWorkItem* deque_steal(HmlWorkStealingDeque* deque) {
    long top = atomic_load_explicit(&deque->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    long bottom = atomic_load_explicit(&deque->bottom, memory_order_acquire);

    if (top < bottom) {
        HmlWorkItem* item = deque->items[top % deque->capacity];

        if (!atomic_compare_exchange_strong_explicit(
                &deque->top, &top, top + 1,
                memory_order_seq_cst, memory_order_relaxed)) {
            return NULL;
        }
        return item;
    }
    return NULL;
}

// ========== SUBMISSION QUEUE IMPLEMENTATION ==========

static HmlSubmissionQueue* submission_queue_new(int capacity) {
    HmlSubmissionQueue* queue = malloc(sizeof(HmlSubmissionQueue));
    if (!queue) return NULL;

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->capacity = capacity;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);

    return queue;
}

static void submission_queue_free(HmlSubmissionQueue* queue) {
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);
    HmlWorkItem* item = queue->head;
    while (item) {
        HmlWorkItem* next = item->next;
        hml_work_item_free(item);
        item = next;
    }
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    free(queue);
}

static int submission_queue_push(HmlSubmissionQueue* queue, HmlWorkItem* item) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->count >= queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
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

static HmlWorkItem* submission_queue_pop(HmlSubmissionQueue* queue) {
    pthread_mutex_lock(&queue->mutex);

    HmlWorkItem* item = queue->head;
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

static HmlWorkItem* submission_queue_pop_wait(HmlSubmissionQueue* queue, int timeout_us) {
    pthread_mutex_lock(&queue->mutex);

    if (!queue->head) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += timeout_us * 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &ts);
    }

    HmlWorkItem* item = queue->head;
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

static HmlWorkItem* work_item_new(HmlWorkItemFunc func, void* data, void* ctx) {
    HmlWorkItem* item = malloc(sizeof(HmlWorkItem));
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

void hml_work_item_free(HmlWorkItem* item) {
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

static void work_item_setup_wait(HmlWorkItem* item) {
    if (!item->mutex) {
        item->mutex = malloc(sizeof(pthread_mutex_t));
        item->cond = malloc(sizeof(pthread_cond_t));
        pthread_mutex_init(item->mutex, NULL);
        pthread_cond_init(item->cond, NULL);
    }
    item->has_waiter = 1;
}

static void work_item_signal_complete(HmlWorkItem* item) {
    atomic_store_explicit(&item->completed, 1, memory_order_release);

    if (item->has_waiter && item->mutex && item->cond) {
        pthread_mutex_lock(item->mutex);
        pthread_cond_signal(item->cond);
        pthread_mutex_unlock(item->mutex);
    }
}

void* hml_work_item_wait(HmlWorkItem* item) {
    if (!item) return NULL;

    if (atomic_load_explicit(&item->completed, memory_order_acquire)) {
        return item->result;
    }

    work_item_setup_wait(item);

    pthread_mutex_lock(item->mutex);
    while (!atomic_load_explicit(&item->completed, memory_order_acquire)) {
        pthread_cond_wait(item->cond, item->mutex);
    }
    pthread_mutex_unlock(item->mutex);

    return item->result;
}

// ========== WORKER IMPLEMENTATION ==========

static int worker_random_victim(HmlWorker* worker) {
    int num_workers = worker->pool->num_workers;
    if (num_workers <= 1) return -1;

    worker->steal_seed = worker->steal_seed * HML_THREADPOOL_STEAL_SEED_MULT + 1;
    int victim = (worker->steal_seed >> 16) % num_workers;

    if (victim == worker->id) {
        victim = (victim + 1) % num_workers;
    }

    return victim;
}

static HmlWorkItem* worker_steal(HmlWorker* worker) {
    HmlThreadPool* pool = worker->pool;

    for (int attempts = 0; attempts < HML_THREADPOOL_STEAL_ATTEMPTS; attempts++) {
        int victim_id = worker_random_victim(worker);
        if (victim_id < 0) break;

        HmlWorker* victim = &pool->workers[victim_id];
        HmlWorkItem* item = deque_steal(victim->deque);

        if (item) {
            atomic_fetch_add(&worker->tasks_stolen, 1);
            return item;
        }
    }

    return NULL;
}

static HmlWorkItem* worker_get_work(HmlWorker* worker) {
    HmlThreadPool* pool = worker->pool;

    HmlWorkItem* item = deque_pop(worker->deque);
    if (item) return item;

    item = submission_queue_pop(pool->submission);
    if (item) return item;

    item = worker_steal(worker);
    if (item) return item;

    return NULL;
}

static void worker_execute(HmlWorker* worker, HmlWorkItem* item) {
    atomic_store(&worker->active, 1);

    void* result = item->func(item->data, item->ctx);
    item->result = result;

    work_item_signal_complete(item);

    atomic_fetch_add(&worker->tasks_executed, 1);
    atomic_store(&worker->active, 0);
}

static void* worker_thread_main(void* arg) {
    HmlWorker* worker = (HmlWorker*)arg;
    HmlThreadPool* pool = worker->pool;

    tls_current_worker = worker;

    // Block all signals in worker thread
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // Signal that we're ready
    pthread_mutex_lock(&pool->start_mutex);
    pthread_cond_signal(&pool->start_cond);
    pthread_mutex_unlock(&pool->start_mutex);

    // Main work loop
    while (!atomic_load(&pool->shutdown)) {
        HmlWorkItem* item = worker_get_work(worker);

        if (item) {
            worker_execute(worker, item);

            if (!item->has_waiter) {
                hml_work_item_free(item);
            }
        } else {
            item = submission_queue_pop_wait(pool->submission,
                                             HML_THREADPOOL_IDLE_SLEEP_US);
            if (item) {
                worker_execute(worker, item);
                if (!item->has_waiter) {
                    hml_work_item_free(item);
                }
            }
        }
    }

    // Drain remaining work
    HmlWorkItem* item;
    while ((item = worker_get_work(worker)) != NULL) {
        worker_execute(worker, item);
        if (!item->has_waiter) {
            hml_work_item_free(item);
        }
    }

    tls_current_worker = NULL;
    return NULL;
}

// ========== THREAD POOL IMPLEMENTATION ==========

static int get_cpu_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count <= 0) count = 4;
    return (int)count;
}

int hml_thread_pool_init(int num_workers) {
    pthread_mutex_lock(&g_pool_mutex);

    if (g_thread_pool) {
        pthread_mutex_unlock(&g_pool_mutex);
        return 0;
    }

    if (num_workers <= 0) {
        num_workers = get_cpu_count();
    }
    if (num_workers < HML_THREADPOOL_MIN_WORKERS) {
        num_workers = HML_THREADPOOL_MIN_WORKERS;
    }
    if (num_workers > HML_THREADPOOL_MAX_WORKERS) {
        num_workers = HML_THREADPOOL_MAX_WORKERS;
    }

    HmlThreadPool* pool = malloc(sizeof(HmlThreadPool));
    if (!pool) {
        pthread_mutex_unlock(&g_pool_mutex);
        return -1;
    }

    pool->num_workers = num_workers;
    atomic_init(&pool->shutdown, 0);
    atomic_init(&pool->started, 0);
    pthread_mutex_init(&pool->start_mutex, NULL);
    pthread_cond_init(&pool->start_cond, NULL);

    pool->submission = submission_queue_new(HML_THREADPOOL_SUBMISSION_QUEUE_CAPACITY);
    if (!pool->submission) {
        free(pool);
        pthread_mutex_unlock(&g_pool_mutex);
        return -1;
    }

    pool->workers = calloc(num_workers, sizeof(HmlWorker));
    if (!pool->workers) {
        submission_queue_free(pool->submission);
        free(pool);
        pthread_mutex_unlock(&g_pool_mutex);
        return -1;
    }

    for (int i = 0; i < num_workers; i++) {
        HmlWorker* worker = &pool->workers[i];
        worker->id = i;
        worker->pool = pool;
        worker->steal_seed = (unsigned int)(i * 1103515245 + 12345);
        atomic_init(&worker->active, 0);
        atomic_init(&worker->tasks_executed, 0);
        atomic_init(&worker->tasks_stolen, 0);

        worker->deque = deque_new(HML_THREADPOOL_DEQUE_INITIAL_CAPACITY);
        if (!worker->deque) {
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
        HmlWorker* worker = &pool->workers[i];
        pthread_create(&worker->thread, NULL, worker_thread_main, worker);
    }

    for (int i = 0; i < num_workers; i++) {
        pthread_cond_wait(&pool->start_cond, &pool->start_mutex);
    }

    atomic_store(&pool->started, 1);
    pthread_mutex_unlock(&pool->start_mutex);

    pthread_mutex_unlock(&g_pool_mutex);
    return 0;
}

void hml_thread_pool_shutdown(void) {
    pthread_mutex_lock(&g_pool_mutex);

    if (!g_thread_pool) {
        pthread_mutex_unlock(&g_pool_mutex);
        return;
    }

    HmlThreadPool* pool = g_thread_pool;

    atomic_store(&pool->shutdown, 1);

    pthread_mutex_lock(&pool->submission->mutex);
    pthread_cond_broadcast(&pool->submission->not_empty);
    pthread_mutex_unlock(&pool->submission->mutex);

    for (int i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->workers[i].thread, NULL);
    }

    for (int i = 0; i < pool->num_workers; i++) {
        deque_free(pool->workers[i].deque);
    }
    free(pool->workers);

    submission_queue_free(pool->submission);

    pthread_mutex_destroy(&pool->start_mutex);
    pthread_cond_destroy(&pool->start_cond);
    free(pool);

    g_thread_pool = NULL;

    pthread_mutex_unlock(&g_pool_mutex);
}

HmlWorkItem* hml_thread_pool_submit(HmlWorkItemFunc func, void* data, void* ctx) {
    HmlThreadPool* pool = g_thread_pool;

    if (!pool) {
        if (hml_thread_pool_init(HML_THREADPOOL_DEFAULT_WORKERS) != 0) {
            return NULL;
        }
        pool = g_thread_pool;
    }

    HmlWorkItem* item = work_item_new(func, data, ctx);
    if (!item) return NULL;

    if (tls_current_worker && tls_current_worker->pool == pool) {
        if (deque_push(tls_current_worker->deque, item) == 0) {
            return item;
        }
    }

    if (submission_queue_push(pool->submission, item) != 0) {
        hml_work_item_free(item);
        return NULL;
    }

    return item;
}

int hml_thread_pool_current_worker_id(void) {
    if (tls_current_worker) {
        return tls_current_worker->id;
    }
    return -1;
}

int hml_thread_pool_is_initialized(void) {
    return g_thread_pool != NULL;
}
