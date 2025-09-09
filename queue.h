#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <time.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t send_time;
    char *msg;
    char *ptr;
    size_t remaining;
    size_t id;
} ScheduledEvent;

typedef struct {
    ScheduledEvent *heap;
    size_t size;
    size_t capacity;
    size_t last_put_idx;
    size_t next_id;
} EventQueue;

void eqInit(EventQueue *q);
void eqDestroy(EventQueue *q);
bool eqEmpty(const EventQueue *q);
void eqPush(EventQueue *q, uint64_t when, const char *msg, bool is_put_response);
ScheduledEvent *eqPeek(const EventQueue *q);
void eqPop(EventQueue *q);
bool eqLastPutSend(EventQueue *q);

#endif