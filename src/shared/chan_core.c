/*
 * Hemlock Channel Core - see chan_core.h for the contract.
 *
 * Design notes
 * ------------
 * Storage: a ring of `slot_cap = max(capacity, 1)` items. An unbuffered
 * channel is therefore a one-slot ring whose sender additionally waits for
 * its item to be taken. "Taken" is detected with `recv_gen`, the number of
 * completed receives: while a sender's item occupies the single slot nothing
 * else can be received, so the first bump of recv_gen after staging is that
 * item's handoff. Comparing a generation (rather than a "slot empty" flag) is
 * what keeps multiple concurrent senders correct: another sender may refill
 * the slot before the first one wakes up.
 *
 * Waiting: receivers wait on `not_empty`, senders on `not_full` (both for a
 * free slot and for the rendezvous handoff). Every wait re-checks its
 * predicate after a timed wait returns, so a wakeup that lands on the same
 * tick as the deadline is never lost and never reported as a timeout.
 *
 * select(): a waiter (mutex + condvar + ready flag) is registered on every
 * channel in the set *before* the channels are scanned, so a send or close
 * that lands between the scan and the wait sets the flag and the wait falls
 * through immediately. Senders and close() notify registered waiters while
 * holding the channel mutex; select() never takes a channel mutex while
 * holding its waiter mutex, so the two lock orders cannot deadlock.
 */
#include "chan_core.h"
#include "hemlock_limits.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
} SelectWaiter;

struct HmlChan {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;      /* receivers wait here */
    pthread_cond_t not_full;       /* senders wait here (slot free, or item taken) */

    unsigned char *slots;          /* ring of slot_cap * item_size bytes */
    size_t item_size;
    int capacity;                  /* user-visible: 0 = unbuffered */
    int slot_cap;                  /* max(capacity, 1) */
    int head;
    int count;
    int closed;
    unsigned long long recv_gen;   /* completed receives (rendezvous handoff detector) */
    HmlChanItemRelease release;

    SelectWaiter **waiters;        /* select() calls currently blocked on this channel */
    int num_waiters;
    int waiters_cap;
};

/* ---------- small helpers (all called with ch->mutex held) ---------- */

static void *slot_ptr(HmlChan *ch, int offset_from_head) {
    size_t idx = (size_t)((ch->head + offset_from_head) % ch->slot_cap);
    return ch->slots + idx * ch->item_size;
}

static void enqueue(HmlChan *ch, const void *item) {
    memcpy(slot_ptr(ch, ch->count), item, ch->item_size);
    ch->count++;
}

static void dequeue(HmlChan *ch, void *item_out) {
    memcpy(item_out, slot_ptr(ch, 0), ch->item_size);
    ch->head = (ch->head + 1) % ch->slot_cap;
    ch->count--;
    ch->recv_gen++;
}

/* Drop the (single) staged rendezvous item without counting it as received. */
static void withdraw_staged(HmlChan *ch) {
    ch->release(slot_ptr(ch, 0));
    ch->count = 0;
}

/* Returns 1 on timeout, 0 on wakeup. */
static int wait_cond(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *deadline) {
    if (!deadline) {
        pthread_cond_wait(cond, mutex);
        return 0;
    }
    return pthread_cond_timedwait(cond, mutex, deadline) == ETIMEDOUT;
}

static void notify_select_waiters(HmlChan *ch) {
    for (int i = 0; i < ch->num_waiters; i++) {
        SelectWaiter *w = ch->waiters[i];
        pthread_mutex_lock(&w->mutex);
        w->ready = 1;
        pthread_cond_signal(&w->cond);
        pthread_mutex_unlock(&w->mutex);
    }
}

/* An item became available. */
static void wake_receivers(HmlChan *ch) {
    pthread_cond_signal(&ch->not_empty);
    notify_select_waiters(ch);
}

/* An item was taken. Unbuffered channels must wake the specific sender whose
 * item was handed off as well as any sender queued for the slot, so broadcast. */
static void wake_senders(HmlChan *ch) {
    if (ch->capacity == 0) {
        pthread_cond_broadcast(&ch->not_full);
    } else {
        pthread_cond_signal(&ch->not_full);
    }
}

static void waiter_add(HmlChan *ch, SelectWaiter *w) {
    if (ch->num_waiters == ch->waiters_cap) {
        int new_cap = ch->waiters_cap ? ch->waiters_cap * 2 : HML_CHAN_WAITERS_INITIAL;
        SelectWaiter **grown = realloc(ch->waiters, sizeof(*grown) * (size_t)new_cap);
        if (!grown) {
            fprintf(stderr, "Runtime error: Memory allocation failed in select()\n");
            abort();
        }
        ch->waiters = grown;
        ch->waiters_cap = new_cap;
    }
    ch->waiters[ch->num_waiters++] = w;
}

static void waiter_remove(HmlChan *ch, SelectWaiter *w) {
    for (int i = 0; i < ch->num_waiters; i++) {
        if (ch->waiters[i] == w) {
            ch->waiters[i] = ch->waiters[--ch->num_waiters];
            return;
        }
    }
}

/* ---------- lifecycle ---------- */

HmlChan *hml_chan_new(int capacity, size_t item_size, HmlChanItemRelease release) {
    if (capacity < 0 || item_size == 0 || !release) {
        return NULL;
    }
    HmlChan *ch = calloc(1, sizeof(HmlChan));
    if (!ch) {
        return NULL;
    }
    ch->capacity = capacity;
    ch->slot_cap = capacity > 0 ? capacity : 1;
    ch->item_size = item_size;
    ch->release = release;
    ch->slots = malloc(item_size * (size_t)ch->slot_cap);
    if (!ch->slots) {
        free(ch);
        return NULL;
    }
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    return ch;
}

void hml_chan_free(HmlChan *ch) {
    if (!ch) {
        return;
    }
    for (int i = 0; i < ch->count; i++) {
        ch->release(slot_ptr(ch, i));
    }
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch->waiters);
    free(ch->slots);
    free(ch);
}

int hml_chan_capacity(const HmlChan *ch) {
    return ch->capacity;
}

int hml_chan_count(HmlChan *ch) {
    pthread_mutex_lock(&ch->mutex);
    int n = ch->count;
    pthread_mutex_unlock(&ch->mutex);
    return n;
}

int hml_chan_is_closed(HmlChan *ch) {
    pthread_mutex_lock(&ch->mutex);
    int c = ch->closed;
    pthread_mutex_unlock(&ch->mutex);
    return c;
}

/* ---------- operations ---------- */

HmlChanStatus hml_chan_send(HmlChan *ch, const void *item, const struct timespec *deadline) {
    pthread_mutex_lock(&ch->mutex);

    /* Phase 1: wait for a free slot. */
    while (ch->count == ch->slot_cap && !ch->closed) {
        if (wait_cond(&ch->not_full, &ch->mutex, deadline) &&
            ch->count == ch->slot_cap && !ch->closed) {
            pthread_mutex_unlock(&ch->mutex);
            ch->release((void *)item);
            return HML_CHAN_TIMEOUT;
        }
    }
    if (ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        ch->release((void *)item);
        return HML_CHAN_CLOSED;
    }

    enqueue(ch, item);
    wake_receivers(ch);

    if (ch->capacity > 0) {
        pthread_mutex_unlock(&ch->mutex);
        return HML_CHAN_OK;
    }

    /* Phase 2 (unbuffered): wait until a receiver takes our item. */
    unsigned long long staged_gen = ch->recv_gen;
    while (ch->recv_gen == staged_gen && !ch->closed) {
        if (wait_cond(&ch->not_full, &ch->mutex, deadline) && ch->recv_gen == staged_gen) {
            withdraw_staged(ch);
            wake_senders(ch);               /* the slot is free again */
            pthread_mutex_unlock(&ch->mutex);
            return HML_CHAN_TIMEOUT;
        }
    }
    if (ch->recv_gen == staged_gen) {       /* closed while our item was staged */
        withdraw_staged(ch);
        pthread_mutex_unlock(&ch->mutex);
        return HML_CHAN_CLOSED;
    }

    pthread_mutex_unlock(&ch->mutex);
    return HML_CHAN_OK;
}

HmlChanStatus hml_chan_recv(HmlChan *ch, void *item_out, const struct timespec *deadline) {
    pthread_mutex_lock(&ch->mutex);

    while (ch->count == 0 && !ch->closed) {
        if (wait_cond(&ch->not_empty, &ch->mutex, deadline) &&
            ch->count == 0 && !ch->closed) {
            pthread_mutex_unlock(&ch->mutex);
            return HML_CHAN_TIMEOUT;
        }
    }
    if (ch->count == 0) {                   /* closed and drained */
        pthread_mutex_unlock(&ch->mutex);
        return HML_CHAN_CLOSED;
    }

    dequeue(ch, item_out);
    wake_senders(ch);
    pthread_mutex_unlock(&ch->mutex);
    return HML_CHAN_OK;
}

void hml_chan_close(HmlChan *ch) {
    pthread_mutex_lock(&ch->mutex);
    ch->closed = 1;
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    notify_select_waiters(ch);
    pthread_mutex_unlock(&ch->mutex);
}

/* Try every channel once, starting at `start`. Returns 1 if something was found. */
static int select_scan(HmlChan *const *chans, int count, int start,
                       int *index_out, void *item_out, HmlChanStatus *status_out) {
    for (int k = 0; k < count; k++) {
        int i = (start + k) % count;
        HmlChan *ch = chans[i];
        pthread_mutex_lock(&ch->mutex);
        if (ch->count > 0) {
            dequeue(ch, item_out);
            wake_senders(ch);
            pthread_mutex_unlock(&ch->mutex);
            *index_out = i;
            *status_out = HML_CHAN_OK;
            return 1;
        }
        if (ch->closed) {
            pthread_mutex_unlock(&ch->mutex);
            *index_out = i;
            *status_out = HML_CHAN_CLOSED;
            return 1;
        }
        pthread_mutex_unlock(&ch->mutex);
    }
    return 0;
}

HmlChanStatus hml_chan_select(HmlChan *const *chans, int count,
                              const struct timespec *deadline,
                              int *index_out, void *item_out) {
    static atomic_uint rotation = 0;
    int start = (int)(atomic_fetch_add(&rotation, 1) % (unsigned)count);

    /* Fast path: something is already ready. */
    HmlChanStatus status;
    if (select_scan(chans, count, start, index_out, item_out, &status)) {
        return status;
    }

    SelectWaiter waiter = { .ready = 0 };
    pthread_mutex_init(&waiter.mutex, NULL);
    pthread_cond_init(&waiter.cond, NULL);

    for (int i = 0; i < count; i++) {
        pthread_mutex_lock(&chans[i]->mutex);
        waiter_add(chans[i], &waiter);
        pthread_mutex_unlock(&chans[i]->mutex);
    }

    for (;;) {
        /* Registered first, so anything that became ready since the last
         * scan has already set (or will set) waiter.ready. */
        if (select_scan(chans, count, start, index_out, item_out, &status)) {
            break;
        }

        pthread_mutex_lock(&waiter.mutex);
        while (!waiter.ready) {
            if (wait_cond(&waiter.cond, &waiter.mutex, deadline)) {
                break;
            }
        }
        int woke = waiter.ready;
        waiter.ready = 0;
        pthread_mutex_unlock(&waiter.mutex);

        if (!woke) {
            status = HML_CHAN_TIMEOUT;
            break;
        }
    }

    for (int i = 0; i < count; i++) {
        pthread_mutex_lock(&chans[i]->mutex);
        waiter_remove(chans[i], &waiter);
        pthread_mutex_unlock(&chans[i]->mutex);
    }
    pthread_cond_destroy(&waiter.cond);
    pthread_mutex_destroy(&waiter.mutex);
    return status;
}

void hml_chan_deadline_after_ms(struct timespec *deadline, long ms) {
    if (ms < 0) {
        ms = 0;
    }
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += ms / HML_MILLISECONDS_PER_SECOND;
    deadline->tv_nsec += (ms % HML_MILLISECONDS_PER_SECOND) * HML_NANOSECONDS_PER_MS;
    if (deadline->tv_nsec >= HML_NANOSECONDS_PER_SECOND) {
        deadline->tv_sec++;
        deadline->tv_nsec -= HML_NANOSECONDS_PER_SECOND;
    }
}
