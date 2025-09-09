#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <netinet/in.h>
#include "cb.h"
#include "queue.h"
#include "common.h"
#include "err.h"

typedef struct {
    bool received_hello;
    bool send_coeffs;
    uint64_t hello_deadline;
    double *coeffs;
    double *approx;
    double penalty;
    size_t put_send;

    CircularBuffer in_buf;
    EventQueue q;

    char *player_id;
    char ipstr[INET6_ADDRSTRLEN];
    uint16_t port;
    uint64_t delay;
} client_t;

static inline void clientInit(client_t *c, size_t n, size_t k) {
    cbInit(&c->in_buf);
    eqInit(&c->q);
    c->hello_deadline = now_ms() + 3000;
    c->coeffs = calloc(n + 1, sizeof *c->coeffs);
    c->approx = calloc(k + 1, sizeof *c->approx);
    if (!c->coeffs || !c->approx) fatal("Out of memory");
    c->received_hello = false;
    c->send_coeffs = false;
    c->penalty = 0;
    c->put_send = 0;
    c->player_id = NULL;
}

static inline void clientDestroy(client_t *c) {
    cbDestroy(&c->in_buf);
    eqDestroy(&c->q);
    free(c->coeffs);
    free(c->approx);
    free(c->player_id);
}

#endif 