#ifndef MIM_MESSAGES_H
#define MIM_MESSAGES_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "cb.h"
#include "queue.h"
#include "client.h"

bool is_valid_player_id(const char *s);
size_t count_lowercase(const char *s);
bool is_valid_bad_put(char *line);
bool is_valid_state_coeff(char *line);
bool is_valid_scoring(char *line); 
bool valid_point_value(char *point_str, char *value_str, size_t *out_point, double *out_value, size_t K);
bool is_valid_put(const char *line, size_t linelen, char** point, char** value);

ssize_t send_hello(const char *player_id, EventQueue *q, int fd);
ssize_t read_message(CircularBuffer *input_messages, int fd);
ssize_t process_data_to_send(EventQueue* q, int fd, char* id);

double *read_coeffs(char* payload, size_t *count);
char *create_penalty_msg(const char *point_str, const char *value_str);
char *create_badput_msg(const char *point_str, const char *value_str);
char *create_put_msg(const char *point, const char *value);
char *create_state_msg(double *approx, size_t K);

bool get_line(CircularBuffer *cb, const char *term, size_t term_len,
    char **line_ptr, size_t *cap_ptr, size_t *out_len);
double calculate_score(size_t n, double* coeffs, double* approx, size_t k, size_t penalty);
double calculate_f(size_t n, double* coeffs, size_t x);
char *create_scoring_msg(client_t **clients, size_t client_count, size_t n, size_t k) ;

#endif