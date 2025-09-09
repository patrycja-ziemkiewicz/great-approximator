#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include "err.h"

static void swap_events(ScheduledEvent *a, ScheduledEvent *b, size_t idx_a, size_t idx_b, EventQueue *q) {
    ScheduledEvent tmp = *a;
    *a = *b;
    *b = tmp;

    if (q->last_put_idx == idx_a)
        q->last_put_idx = idx_b;
    else if (q->last_put_idx == idx_b)
        q->last_put_idx = idx_a;
}

void eqInit(EventQueue *q) {
    q->heap = malloc(sizeof(ScheduledEvent) * 8);
    if (!q->heap) fatal("Out of memory");
    q->size = 0;
    q->capacity = 8;
    q->last_put_idx = SIZE_MAX;
    q->next_id = 0;
}

void eqDestroy(EventQueue *q) {
    for (size_t i = 0; i < q->size; ++i) {
        free(q->heap[i].msg);
    }
    free(q->heap);
    q->heap = NULL;
    q->size = q->capacity = 0;
    q->last_put_idx = SIZE_MAX;
}

bool eqEmpty(const EventQueue *q) {
    return q->size == 0;
}

void eqPush(EventQueue *q, uint64_t when, const char *msg, bool is_put_response) {
    if (q->size + 1 > q->capacity) {
        size_t new_cap = q->capacity * 2;
        ScheduledEvent *tmp = realloc(q->heap, new_cap * sizeof *tmp);
        if (!tmp) fatal("Out of memory");
        q->heap = tmp;
        q->capacity = new_cap;
    }

    ScheduledEvent *evt = &q->heap[q->size];
    evt->send_time = when;
    evt->id = q->next_id++;
    evt->msg = strdup(msg);
    if (!evt->msg) fatal("Out of memory");
    evt->ptr = evt->msg;
    evt->remaining = strlen(evt->msg);

    size_t idx = q->size++;
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        bool need_swap = false;
        if (q->heap[parent].send_time > q->heap[idx].send_time) {
            need_swap = true;
        } else if (q->heap[parent].send_time == q->heap[idx].send_time &&
                   q->heap[parent].id > q->heap[idx].id) {
            need_swap = true;
        }

        if (!need_swap)
            break;

        swap_events(&q->heap[parent], &q->heap[idx], parent, idx, q);
        idx = parent;
    }

    if (is_put_response) {
        q->last_put_idx = idx;
    }
}

void eqUpdate(EventQueue *q, size_t n) {
    if (q->size == 0) return;
    q->heap[0].ptr += n;
    q->heap[0].remaining -= n;
}

ScheduledEvent *eqPeek(const EventQueue *q) {
    if (q->size == 0) return NULL;
    return &q->heap[0];
}

void eqPop(EventQueue *q) {
    if (q->size == 0) return;
    if (q->last_put_idx == 0) {
        q->last_put_idx = SIZE_MAX;
    }

    free(q->heap[0].msg);
    q->heap[0] = q->heap[--q->size];
    if (q->last_put_idx == q->size) {
        q->last_put_idx = 0;
    }

    size_t idx = 0;
    while (true) {
        size_t left = 2 * idx + 1;
        size_t right = left + 1;
        size_t smallest = idx;

        if (left < q->size) {
            if (q->heap[left].send_time < q->heap[smallest].send_time ||
                (q->heap[left].send_time == q->heap[smallest].send_time &&
                 q->heap[left].id < q->heap[smallest].id)) {
                smallest = left;
            }
        }
        if (right < q->size) {
            if (q->heap[right].send_time < q->heap[smallest].send_time ||
                (q->heap[right].send_time == q->heap[smallest].send_time &&
                 q->heap[right].id < q->heap[smallest].id)) {
                smallest = right;
            }
        }

        if (smallest == idx)
            break;

        swap_events(&q->heap[idx], &q->heap[smallest], idx, smallest, q);
        idx = smallest;
    }
}

bool eqLastPutSend(EventQueue *q) {
    return (q->last_put_idx == SIZE_MAX);
}
