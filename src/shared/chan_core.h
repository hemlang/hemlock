/*
 * Hemlock Channel Core
 *
 * Backend-independent channel implementation shared by the interpreter and
 * the compiled-program runtime. It owns the queueing and blocking logic:
 *
 *   - a bounded FIFO ring of fixed-size items (capacity > 0), or
 *   - a rendezvous slot (capacity == 0): send() returns only after a receiver
 *     has taken the item;
 *   - close(): wakes everyone, further sends fail, receives drain then
 *     report closed;
 *   - deadlines on every blocking operation;
 *   - select(): blocks on a set of channels without polling.
 *
 * The core never looks inside an item. It copies `item_size` bytes in on
 * send and out on receive, and calls `release` on any item it has to drop
 * itself (a withdrawn rendezvous item, or items still queued at free time).
 * Ownership rule for callers: the reference passed to hml_chan_send() is
 * consumed on every return path; the reference handed out by
 * hml_chan_recv()/hml_chan_select() on HML_CHAN_OK belongs to the caller.
 *
 * All functions are thread-safe. Lock order: a channel's mutex may be held
 * while taking a select waiter's mutex, never the reverse.
 */
#ifndef HEMLOCK_CHAN_CORE_H
#define HEMLOCK_CHAN_CORE_H

#include <stddef.h>
#include <time.h>

typedef enum {
    HML_CHAN_OK = 0,
    HML_CHAN_CLOSED,   /* send: channel closed (item released);
                          recv/select: closed and drained (no item) */
    HML_CHAN_TIMEOUT   /* deadline passed before the operation could complete */
} HmlChanStatus;

typedef void (*HmlChanItemRelease)(void *item);

typedef struct HmlChan HmlChan;

/* capacity 0 = rendezvous (unbuffered). Returns NULL on allocation failure. */
HmlChan *hml_chan_new(int capacity, size_t item_size, HmlChanItemRelease release);

/* Releases every item still queued. Must not race with other operations. */
void hml_chan_free(HmlChan *ch);

int hml_chan_capacity(const HmlChan *ch);   /* user-visible capacity (0 = unbuffered) */
int hml_chan_count(HmlChan *ch);            /* items currently queued */
int hml_chan_is_closed(HmlChan *ch);

/* deadline == NULL blocks indefinitely. Deadlines are CLOCK_REALTIME. */
HmlChanStatus hml_chan_send(HmlChan *ch, const void *item, const struct timespec *deadline);
HmlChanStatus hml_chan_recv(HmlChan *ch, void *item_out, const struct timespec *deadline);
void hml_chan_close(HmlChan *ch);

/*
 * Wait until one of `chans` has an item (HML_CHAN_OK: *index_out and
 * *item_out set) or is closed and drained (HML_CHAN_CLOSED: *index_out set).
 * HML_CHAN_TIMEOUT if the deadline passes first. Channels are examined in
 * rotating order so a busy channel cannot starve the others.
 */
HmlChanStatus hml_chan_select(HmlChan *const *chans, int count,
                              const struct timespec *deadline,
                              int *index_out, void *item_out);

/* Absolute CLOCK_REALTIME deadline `ms` milliseconds from now (ms < 0 => now). */
void hml_chan_deadline_after_ms(struct timespec *deadline, long ms);

#endif /* HEMLOCK_CHAN_CORE_H */
