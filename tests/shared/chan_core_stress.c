/*
 * Stress test for the shared channel core (src/shared/chan_core.c).
 *
 * Runs many producers, consumers and select()ors against buffered and
 * unbuffered channels, with and without deadlines, then closes the channels
 * and checks that every item sent was received exactly once and every item
 * allocated was released exactly once (leaks and double releases both show
 * up as a count mismatch). Also pins the rendezvous contract: an unbuffered
 * send() returns only after its item was taken, a timed-out send withdraws
 * its item, and a send on a closed channel fails.
 *
 *   make test-chan-core           # plain build
 *   make test-chan-core TSAN=1    # same, under ThreadSanitizer
 */
#include "chan_core.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define N_PRODUCERS 4
#define N_CONSUMERS 3
#define N_SELECTORS 3
#define PER_PRODUCER 2000

typedef struct { long value; } Item;
static atomic_long allocated = 0, released = 0, consumed = 0, consumed_sum = 0, sent_sum = 0, timed_out_sends = 0;

static void release_item(void *slot) {
    Item *it = *(Item **)slot;
    free(it);
    atomic_fetch_add(&released, 1);
}
static Item *mk(long v) { Item *it = malloc(sizeof *it); it->value = v; atomic_fetch_add(&allocated, 1); return it; }

typedef struct { HmlChan *ch; int id; int use_timeout; } Arg;

static void *producer(void *p) {
    Arg *a = p;
    for (long i = 0; i < PER_PRODUCER; i++) {
        long v = a->id * 1000000L + i;
        for (;;) {
            Item *it = mk(v);
            struct timespec dl; hml_chan_deadline_after_ms(&dl, 1);
            HmlChanStatus st = hml_chan_send(a->ch, &it, a->use_timeout ? &dl : NULL);
            if (st == HML_CHAN_OK) { atomic_fetch_add(&sent_sum, v); break; }
            assert(st == HML_CHAN_TIMEOUT);  // item released by the core
            atomic_fetch_add(&timed_out_sends, 1);
        }
    }
    return NULL;
}

static void *consumer(void *p) {
    Arg *a = p;
    for (;;) {
        Item *it;
        struct timespec dl; hml_chan_deadline_after_ms(&dl, 1);
        HmlChanStatus st = hml_chan_recv(a->ch, &it, a->use_timeout ? &dl : NULL);
        if (st == HML_CHAN_CLOSED) break;
        if (st == HML_CHAN_TIMEOUT) continue;
        atomic_fetch_add(&consumed, 1); atomic_fetch_add(&consumed_sum, it->value);
        free(it); atomic_fetch_add(&released, 1);
    }
    return NULL;
}

typedef struct { HmlChan *chans[2]; } SelArg;
static void *selector(void *p) {
    SelArg *a = p;
    for (;;) {
        Item *it; int idx;
        struct timespec dl; hml_chan_deadline_after_ms(&dl, 2);
        HmlChanStatus st = hml_chan_select(a->chans, 2, &dl, &idx, &it);
        if (st == HML_CHAN_TIMEOUT) continue;
        if (st == HML_CHAN_CLOSED) {
            break;  // both channels close together; leftovers are drained by run()
        }
        atomic_fetch_add(&consumed, 1); atomic_fetch_add(&consumed_sum, it->value);
        free(it); atomic_fetch_add(&released, 1);
    }
    return NULL;
}

static void run(int capacity, int use_timeout) {
    atomic_store(&allocated, 0); atomic_store(&released, 0); atomic_store(&consumed, 0);
    atomic_store(&consumed_sum, 0); atomic_store(&sent_sum, 0); atomic_store(&timed_out_sends, 0);
    HmlChan *ch = hml_chan_new(capacity, sizeof(Item *), release_item);
    HmlChan *ch2 = hml_chan_new(capacity, sizeof(Item *), release_item);
    pthread_t prod[N_PRODUCERS], cons[N_CONSUMERS], sel[N_SELECTORS];
    Arg pa[N_PRODUCERS], ca[N_CONSUMERS]; SelArg sa[N_SELECTORS];
    for (int i = 0; i < N_PRODUCERS; i++) { pa[i] = (Arg){ i % 2 ? ch : ch2, i + 1, use_timeout }; pthread_create(&prod[i], NULL, producer, &pa[i]); }
    for (int i = 0; i < N_CONSUMERS; i++) { ca[i] = (Arg){ i % 2 ? ch : ch2, i, use_timeout }; pthread_create(&cons[i], NULL, consumer, &ca[i]); }
    for (int i = 0; i < N_SELECTORS; i++) { sa[i].chans[0] = ch; sa[i].chans[1] = ch2; pthread_create(&sel[i], NULL, selector, &sa[i]); }
    for (int i = 0; i < N_PRODUCERS; i++) pthread_join(prod[i], NULL);
    hml_chan_close(ch); hml_chan_close(ch2);
    for (int i = 0; i < N_CONSUMERS; i++) pthread_join(cons[i], NULL);
    for (int i = 0; i < N_SELECTORS; i++) pthread_join(sel[i], NULL);
    // Drain whatever consumers left after close (selectors may have exited early on CLOSED)
    Item *it;
    while (hml_chan_recv(ch, &it, NULL) == HML_CHAN_OK) { atomic_fetch_add(&consumed, 1); atomic_fetch_add(&consumed_sum, it->value); free(it); atomic_fetch_add(&released, 1); }
    while (hml_chan_recv(ch2, &it, NULL) == HML_CHAN_OK) { atomic_fetch_add(&consumed, 1); atomic_fetch_add(&consumed_sum, it->value); free(it); atomic_fetch_add(&released, 1); }
    hml_chan_free(ch); hml_chan_free(ch2);
    long expected = (long)N_PRODUCERS * PER_PRODUCER;
    printf("cap=%d timeout=%d: consumed=%ld/%ld sum_ok=%d alloc=%ld released=%ld timed_out_sends=%ld\n",
           capacity, use_timeout, atomic_load(&consumed), expected,
           atomic_load(&consumed_sum) == atomic_load(&sent_sum), atomic_load(&allocated), atomic_load(&released), atomic_load(&timed_out_sends));
    assert(atomic_load(&consumed) == expected);
    assert(atomic_load(&consumed_sum) == atomic_load(&sent_sum));
    assert(atomic_load(&allocated) == atomic_load(&released));
}

// Rendezvous contract: an unbuffered send must not return before its item was received.
static atomic_int handoff_seen = 0;
static void *slow_receiver(void *p) {
    HmlChan *ch = p; Item *it;
    struct timespec ts = {0, 50 * 1000000L}; nanosleep(&ts, NULL);
    assert(hml_chan_recv(ch, &it, NULL) == HML_CHAN_OK);
    atomic_store(&handoff_seen, 1);
    free(it); atomic_fetch_add(&released, 1);
    return NULL;
}
static void rendezvous_test(void) {
    HmlChan *ch = hml_chan_new(0, sizeof(Item *), release_item);
    pthread_t t; pthread_create(&t, NULL, slow_receiver, ch);
    Item *it = mk(7);
    assert(hml_chan_send(ch, &it, NULL) == HML_CHAN_OK);
    assert(atomic_load(&handoff_seen) == 1);  // send returned only after recv took it
    pthread_join(t, NULL);
    // closing with a staged sender: the sender's send fails with CLOSED
    Item *it2 = mk(8);
    struct timespec dl; hml_chan_deadline_after_ms(&dl, 20);
    assert(hml_chan_send(ch, &it2, &dl) == HML_CHAN_TIMEOUT);
    assert(hml_chan_count(ch) == 0);  // withdrawn
    hml_chan_close(ch);
    Item *it3 = mk(9);
    assert(hml_chan_send(ch, &it3, NULL) == HML_CHAN_CLOSED);
    hml_chan_free(ch);
    printf("rendezvous: ok\n");
}

int main(void) {
    rendezvous_test();
    run(0, 0); run(0, 1); run(1, 0); run(16, 1); run(1000, 0);
    printf("ALL OK\n");
    return 0;
}
