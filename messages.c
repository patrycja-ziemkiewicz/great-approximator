#include "messages.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <float.h>
#include <regex.h>

#include "err.h"
#include "common.h"
#include "cb.h"
#include "queue.h"
#include "client.h"

#define BUF_SIZE 10000

bool is_matching(const char *pattern, const char *line) {
    regex_t regex;
    int ret;

    ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {

        return false;
    }

    ret = regexec(&regex, line, 0, NULL, 0);
    regfree(&regex);

    return (ret == 0);
}

bool is_valid_player_id(const char *s) {
    if (!s || !*s) return false;
    for (const char *p = s; *p; ++p) {
        if (!isalnum((unsigned char)*p)) return false;
    }
    return true;
}

size_t count_lowercase(const char *s) {
    size_t cnt = 0;
    for (const char *p = s; *p; ++p) {
        if (*p >= 'a' && *p <= 'z')
            ++cnt;
    }
    return cnt;
}

static bool is_rational(const char *s, size_t len) {
    if (*s == '-') {
        s++;
        len--;
    }

    if (len == 0) return false;
    if (!isdigit((unsigned char)*s)) return false;
    while (isdigit((unsigned char)*s) && len > 0) {
        s++;
        len--;
    }

    if (len == 0) return true;
    if (*s != '.') return false;
    s++;
    len--;
    if (len > 7) return false;

    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)*s)) 
            return false;
        s++;
    }
    return true;
}

bool is_valid_state_coeff(char *line) {
    const char *pattern = "^-?[0-9]+(\\.[0-9]{0,7})?( -?[0-9]+(\\.[0-9]{0,7})?)*$";
    return is_matching(pattern, line);
}

bool is_valid_bad_put(char *line) {
    const char *pattern = "^-?[0-9]+(\\.[0-9]{0,7})? -?[0-9]+(\\.[0-9]{0,7})?$";
    return is_matching(pattern, line);
}

bool is_valid_scoring(char *line) {
    const char *pattern = "^[A-Za-z0-9]+ -?[0-9]+(\\.[0-9]{0,7})?( [A-Za-z0-9]+ -?[0-9]+(\\.[0-9]{0,7})?)*$";
    return is_matching(pattern, line);
}

bool is_valid_put(const char *line, size_t linelen, char** point, char** value) {
    char *sp = memchr(line, ' ', linelen);
    if (!sp) {
        return false;
    }

    size_t point_len = sp - line;
    size_t value_len = linelen - point_len - 1; // odejmuje 2 bo ma na koncu \n

    if (!is_rational(line, point_len) || !is_rational(sp+1, value_len)) {
        return false;
    }

    *sp = '\0';

    *point = (char *)line;
    *value = (char *)(sp + 1);

    return true;
}

bool is_valid_point(char *line) {
    const char *pattern = "^[0-9]+(\\.[0]{0,7})?$";
    return is_matching(pattern, line);
}

bool valid_point_value(char *point_str, char *value_str,
        size_t *out_point, double *out_value, size_t K) {

    if (!is_valid_point(point_str))
        return false;

    char *endptr;
    errno = 0;

    double point = strtod(point_str, &endptr);
    if (errno != 0 || *endptr != '\0' || point > (double) K || point < 0) {
        return false;
    }

    errno = 0;
    double value = strtod(value_str, &endptr);
    if (errno != 0 || *endptr != '\0' || value < -5.0 || value > 5.0) {
        return false;
    }

    *out_point = (size_t) point;
    *out_value = value;
    return true;
}

ssize_t process_data_to_send(EventQueue* q, int fd, char* id) {
    uint64_t now = now_ms();
    ScheduledEvent *evt;

    while(!eqEmpty(q) && eqPeek(q)->send_time <= now) {
        evt = eqPeek(q);

        ssize_t bytes_send = write(fd, evt->ptr, evt->remaining);

        if (bytes_send < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return -2;
            return -1;
        }

        evt->ptr += (size_t)bytes_send;
        evt->remaining -= (size_t)bytes_send;
        
        if (evt->remaining == 0) {
            printf("Sending %s message: %s", id, evt->msg);
            eqPop(q);
        }
        else {
            return 0;
        }
    }
    return 1;
}

ssize_t read_message(CircularBuffer *input_messages, int fd) {
    char buffer[BUF_SIZE];
    
    ssize_t n = read(fd, buffer, BUF_SIZE);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -2;
        return -1;
    }

    cbPushBack(input_messages, buffer, n); 
    return n;
}

ssize_t send_hello(const char *player_id, EventQueue *q, int fd) {

    size_t len = 6 + strlen(player_id) + 2 + 1;
    char *buf = malloc(len);
    if (!buf) fatal("Out of memory");
    snprintf(buf, len, "HELLO %s\r\n", player_id);

    eqPush(q, now_ms(), buf, false);
    free(buf);

    while(!eqEmpty(q)) {
        if (process_data_to_send(q, fd, "server") < 0)
            return -1;
    }
    return 1;
}

double *read_coeffs(char* payload, size_t *count) {

    size_t coeff_count = 0;
    char *copy = strdup(payload);
    if (!copy) {
        syserr("strdup");
    }

    char *saveptr, *tok;
    for (tok = strtok_r(copy, " ", &saveptr);tok != NULL; tok = strtok_r(NULL, " ", &saveptr)) {
        coeff_count++;
    }

    free(copy);

    double *coeffs = malloc(coeff_count * sizeof *coeffs);
    if (!coeffs) 
        fatal("Out of memory");

    size_t id = 0;
    for (tok = strtok_r(payload, " ", &saveptr);tok != NULL; tok = strtok_r(NULL, " ", &saveptr)) {
        coeffs[id++] = strtod(tok, NULL);
    }

    *count = coeff_count;

    return coeffs;
}

char *create_penalty_msg(const char *point_str, const char *value_str) {
    size_t len = 8 + strlen(point_str) + 1 + strlen(value_str) + 2 + 1;
    char *buf = malloc(len);
    if (!buf) fatal("Out of memory");
    snprintf(buf, len, "PENALTY %s %s\r\n", point_str, value_str);
    return buf;
}

char *create_badput_msg(const char *point_str, const char *value_str) {
    size_t len = 8 + strlen(point_str) + 1 + strlen(value_str) + 2 + 1;
    char *buf = malloc(len);
    if (!buf) fatal("Out of memory");
    snprintf(buf, len, "BAD_PUT %s %s\r\n", point_str, value_str);
    return buf;
}

char *create_put_msg(const char *point, const char *value) {
    size_t len = 4 + strlen(point) + 1 + strlen(value) + 2 + 1;
    char *buf = malloc(len);
    if (!buf) fatal("Out of memory");
    snprintf(buf, len, "PUT %s %s\r\n", point, value);
    return buf;
}

char *create_state_msg(double *approx, size_t K) {
    size_t estimate = 6 + (K + 1) * 32 + K + 2 + 1;
    char *buf = malloc(estimate);
    if (!buf) fatal("Out of memory");
    char *p = buf;
    size_t remaining = estimate;

    int n = snprintf(p, remaining, "STATE");
    p += n; 
    remaining -= n;

    for (size_t i = 0; i <= K; ++i) {
        *p++ = ' '; 
        remaining--;
        n = snprintf(p, remaining, "%.7f", approx[i]);
        p += n; 
        remaining -= n;
    }
    *p++ = '\r'; 
    *p++ = '\n'; 
    *p = '\0';

    return buf;
}

bool get_line(CircularBuffer *cb,
                  const char *term, size_t term_len,
                  char **line_ptr, size_t *cap_ptr,
                  size_t *out_len)
{
    size_t total_len;
    if ((total_len = cbGetLineLen(cb, term, term_len)) == 0) {
        return false;
    }

    size_t content_len = total_len - term_len;


    if (content_len + 1 > *cap_ptr) {
        size_t new_cap = content_len + 1;
        char *tmp = realloc(*line_ptr, new_cap);
        if (!tmp) fatal("Out of memory");
        *line_ptr = tmp;
        *cap_ptr   = new_cap;
    }

    cbGetLine(cb, *line_ptr, term, term_len, *cap_ptr);

    *out_len = content_len;
    return true;
}

double calculate_f(size_t n, double* coeffs, size_t x) {
    double fx = 0.0;
    double xi = 1.0;
    for (size_t i = 0; i <= n; i++) {
        fx  += coeffs[i] * xi;
        xi  *= (double)x;
    }
    return fx;
}

double calculate_score(size_t n, double* coeffs, double* approx, size_t k, size_t penalty) {
    double score = (double)penalty;
    for (size_t x = 0; x <= k; x++) {
        double fx = calculate_f(n, coeffs, x);
        double diff = approx[x] - fx;
        score += diff * diff;
    }
    return score;
}

static int cmp_client_by_id(const void *pa, const void *pb) {
    const client_t *const *a = pa;
    const client_t *const *b = pb;
    return strcmp((*a)->player_id, (*b)->player_id);
}

char *create_scoring_msg(client_t **clients, size_t client_count, size_t n, size_t k) {
    client_t **arr = malloc(client_count * sizeof *arr);
    if (!arr) fatal("Out of memory");
    memcpy(arr, clients, client_count * sizeof *arr);
    qsort(arr, client_count, sizeof *arr, cmp_client_by_id);

    double *scores = malloc(client_count * sizeof *scores);
    size_t *score_lengths = malloc(client_count * sizeof *score_lengths);
    if (!scores || !score_lengths) fatal("Out of memory");

    for (size_t i = 0; i < client_count; i++) {
        double sc = calculate_score(n, arr[i]->coeffs, arr[i]->approx, k, arr[i]->penalty);
        score_lengths[i] = (size_t)snprintf(NULL, 0, "%.7f", sc);
        scores[i] = sc;
    }

    size_t buflen = strlen("SCORING") + 1;
    for (size_t i = 0; i < client_count; i++) {
        buflen += strlen(arr[i]->player_id) + 1 + score_lengths[i] + 1;
    }
    buflen += 2 + 1;

    char *buf = malloc(buflen);
    if (!buf) fatal("Out of memory");

    char *p = buf;
    int written = snprintf(p, buflen, "SCORING");
    p += written;
    buflen -= written;

    for (size_t i = 0; i < client_count; i++) {
        written = snprintf(p, buflen, " %s %.7f",
                           arr[i]->player_id, scores[i]);
        p += written;
        buflen -= written;
    }

    snprintf(p, buflen, "\r\n");

    free(arr);
    free(scores);
    free(score_lengths);
    return buf;
}