#define _GNU_SOURCE
#include <endian.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdint.h>

#include "err.h"
#include "common.h"
#include "messages.h"
#include "cb.h"
#include "queue.h"

#define POLL_TIMEOUT 1000
#define MAX_PUT_SIZE 23

static client_params params;
static bool finish = false;
static bool received_response = false;
static bool received_coeffs = false;
static double *coeffs = NULL;
static size_t coeff_count = 0;
static size_t current_point = 0;
static double current_value = 0;

void process_input(CircularBuffer *input_messages, EventQueue *q) {
    char *line = NULL;
    size_t cap  = 0;
    size_t len;

    while (get_line(input_messages, "\n", 1, &line, &cap, &len)) {

        char* point, *value;
        if (!is_valid_put(line, len, &point, &value)) {
            error("invalid input line %.*s", (int)len, line);
        }
        else {
            char * msg = create_put_msg(point, value);
            eqPush(q, now_ms(), msg, false);
            free(msg); 
        }
    }
    free(line);
}

void process_server_message(CircularBuffer *server_messages) {
    size_t len;
    size_t cap = 0;
    char *line = NULL;

    while (get_line(server_messages, "\r\n", 2, &line, &cap, &len) && !finish) {
        if (!received_coeffs) {
            if (strncmp(line, "COEFF ", 6) == 0 && is_valid_state_coeff(line + 6)) {
                char *payload = line + 6;
                
                printf("Received coefficients %s\n", line + 6);
                coeffs = read_coeffs(payload, &coeff_count);
                received_response = true;
                received_coeffs = true;
            }
            else {
                error_msg((char*)params.server_addr, "server", params.port, line);
            }           
        }
        else {
            if (strncmp(line, "STATE ", 6) == 0 && is_valid_state_coeff(line + 6)) {
                printf("Received state %s.\n", line + 6);
                received_response = true;
            }
            else if (strncmp(line, "SCORING ", 8) == 0 && is_valid_scoring(line + 8)) {
                printf("Game end, scoring: %s.\n", line + 8);
                finish = true;
            }
            else if (strncmp(line, "BAD_PUT ", 8) == 0 && is_valid_bad_put(line + 8)) {
                printf("Received BAD_PUT %s.\n", line + 8);
            }
            else if (strncmp(line, "PENALTY ", 8) == 0  && is_valid_bad_put(line + 8)) {
                printf( "Received PENALTY %s.\n", line + 8);
            }
            else {
                error_msg((char*)params.server_addr, "server", params.port, line);
            }
        }
    }
    free(line); 
}

void send_next(EventQueue *q) {
    double sc = calculate_f(coeff_count, coeffs, current_point);
    sc -= current_value;
    while (sc == 0) {
        current_value = 0;
        current_point++;
        sc = calculate_f(coeff_count, coeffs, current_point);
    }

    char *msg = malloc(MAX_PUT_SIZE);

    if (sc >= 5) {
        snprintf(msg, MAX_PUT_SIZE, "PUT %zu 5\r\n", current_point);
        current_value += 5;
    }
    else if (sc <= -5) {
        snprintf(msg,MAX_PUT_SIZE, "PUT %zu -5\r\n", current_point);
        current_value -= 5;
    }
    else {
        snprintf(msg, MAX_PUT_SIZE, "PUT %zu %.7f\r\n", current_point, sc);
        current_point++;
        current_value = 0;
    }
    
    eqPush(q, now_ms(), msg, false);
    received_response = false;
    free(msg);
}

void clean_up(EventQueue *q, struct pollfd *fds) {

    uint64_t now = now_ms();

    if (!eqEmpty(q) && eqPeek(q)->send_time <= now && received_coeffs) {
        fds[0].events = POLLIN | POLLOUT;
    }
    else {
        fds[0].events = POLLIN;
    }

}

int main(int argc, char *argv[]) {

    read_params_client(argc, argv, &params);

    int socket_fd;
    if (params.ipv4) {
        struct sockaddr_in server_address = get_server_addr_ipv4(params.server_addr, params.port);
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);

        if (socket_fd < 0) {
            syserr("cannot create a socket");
        }

        if (connect(socket_fd, (struct sockaddr *) &server_address,
                (socklen_t) sizeof(server_address)) < 0) {
            syserr("cannot connect to the server");
        }
        char const *server_ip = inet_ntoa(server_address.sin_addr);
        uint16_t server_port = ntohs(server_address.sin_port);
        printf("Connected to [%s]:%" PRIu16 "\n",
               server_ip, server_port);
    } 
    else {
        struct sockaddr_in6 server_address = get_server_addr_ipv6(params.server_addr, params.port);
        socket_fd = socket(AF_INET6, SOCK_STREAM, 0);

        if (socket_fd < 0) {
            syserr("cannot create a socket");
        }

        if (connect(socket_fd, (struct sockaddr *) &server_address,
                (socklen_t) sizeof(server_address)) < 0) {
            syserr("cannot connect to the server");
        }

        char ip6str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &server_address.sin6_addr, ip6str, sizeof ip6str);
        printf("Connected to [%s]:%hu\n", ip6str, ntohs(server_address.sin6_port));
    }

    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK)) {
        syserr("fcntl");
    }

    CircularBuffer server_messages;
    CircularBuffer input_messages;
    EventQueue messages_to_send;

    eqInit(&messages_to_send);
    cbInit(&server_messages);
    cbInit(&input_messages);

    struct pollfd fds[2];
    fds[0].fd = socket_fd;
    fds[0].events = POLLIN;
    fds[1].fd = STDIN_FILENO;
    if (!params.a) {
        fds[1].events = POLLIN;
    }
    else {
        fds[1].events = 0;
    }

    if(send_hello(params.id, &messages_to_send, socket_fd) < 0) {
        fatal("can't send hello");
    }

    while(!finish) {
        int poll_status = poll(fds, 2, POLL_TIMEOUT);
        if (poll_status == -1 ) {
            if (errno == EINTR) {
                continue;
            }
            else {
                syserr("poll");
            }
        }
        else if (poll_status > 0) {
            if (fds[0].revents & (POLLIN | POLLERR)) { 
                ssize_t received_bytes = read_message(&server_messages, socket_fd);
                if (received_bytes == -1) {
                    fatal("read");
                }
                else if (received_bytes == 0) {
                    fatal("unexpected server disconnect\n");
                }
                else if (received_bytes > 0) {
                    process_server_message(&server_messages);
                }
            }
            if ((fds[0].revents & POLLOUT)) {
                ssize_t send = process_data_to_send(&messages_to_send, socket_fd, "server");
                if (send == -1){
                    fatal("write");
                }
            }
            if (fds[1].revents & (POLLIN | POLLERR)) {
                ssize_t received_bytes = read_message(&input_messages, STDIN_FILENO);
                if (received_bytes == -1) {
                    fatal("read std");
                }
                else if (received_bytes == 0) {
                    fds[1].events = 0;
                }
                else if (received_bytes > 0){
                    process_input(&input_messages, &messages_to_send);
                }
            }
        }
        clean_up(&messages_to_send, fds);

        if (params.a && received_response) {
            send_next(&messages_to_send);
        }
    }

    cbDestroy(&input_messages);
    cbDestroy(&server_messages);
    eqDestroy(&messages_to_send);
    close(socket_fd);
    free(coeffs);
    return 0;
}
