#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>

#include "err.h"
#include "common.h"
#include "messages.h"
#include "queue.h"
#include "cb.h"
#include "client.h"

#define CONNECTIONS 1024
#define TIMEOUT 1000

static bool finish = false;
static bool finish_game = false;
static server_params params;
static size_t received_puts = 0;
static int active_clients = 0;

// Find slot for a new client.
int find_slot(struct pollfd *fds, client_t *clients, int client_fd, struct sockaddr* addr) {
    if (fcntl(client_fd, F_SETFL, O_NONBLOCK)) {
        syserr("fcntl");
    }

    if (active_clients + 2 < CONNECTIONS) {
        size_t idx = active_clients;
        fds[idx+2].fd = client_fd;
        fds[idx+2].events = POLLIN;

        clientInit(&clients[idx], params.n, params.k);
        client_t *c = &clients[idx];

        if (addr->sa_family == AF_INET) {
            struct sockaddr_in *a4 = (struct sockaddr_in*)addr;
            inet_ntop(AF_INET, &a4->sin_addr, c->ipstr, sizeof c->ipstr);
            c->port = ntohs(a4->sin_port);
        } else {
            struct sockaddr_in6 *a6 = (struct sockaddr_in6*)addr;
            inet_ntop(AF_INET6, &a6->sin6_addr, c->ipstr, sizeof c->ipstr);
            c->port = ntohs(a6->sin6_port);
        }
        printf("New client [%s]:%hu\n", c->ipstr, c->port);
        active_clients++;
        return true; 
    }
    return false;
}

void end_connection(client_t *clients, struct pollfd *pfd, int id) {
    int last = active_clients - 1;
    if (id != last) {

        client_t tmp = clients[id];
        clients[id] = clients[last];
        clients[last] = tmp;
        
        struct pollfd tmpf = pfd[id + 2];
        pfd[id + 2] = pfd[last + 2];
        pfd[last + 2] = tmpf;
    }

    received_puts -= clients[last].put_send;
    clientDestroy(&clients[last]);
    close(pfd[last + 2].fd);
    pfd[last + 2].fd= -1;

    active_clients--;
}

void end_game(struct pollfd *fds, client_t* clients){
    client_t *ptrs[active_clients];
    size_t  ptrs_count = 0;

    for (int i = 0; i < active_clients; i++) {
        client_t *c = &clients[i];
        ptrs[ptrs_count++] = c;
    }
    char* msg = create_scoring_msg(ptrs, ptrs_count, params.n, params.k);
    for (int i = active_clients - 1; i >= 0; i--) {

        write(fds[i + 2].fd, msg, strlen(msg));
        end_connection(clients, fds, i);
    }
    printf("Game end, scoring: %s.", msg + 8);
    finish_game = false;
    free(msg);
    sleep(1);
}

void process_put(client_t *c, char* point_str, char* value_str) {
    double value;
    size_t point;
    uint64_t now = now_ms();

    if (!eqLastPutSend(&c->q) || !c->send_coeffs) {
        char * msg = create_penalty_msg(point_str, value_str);
        eqPush(&c->q, now, msg, false);
        free(msg); 
        c->penalty += 20;
    }
    if (!valid_point_value(point_str, value_str, &point, &value, params.k)) {
        char * msg = create_badput_msg(point_str, value_str);
        eqPush(&c->q, now + 1000, msg, true);
        free(msg); 
        c->penalty += 10;
    }
    else {
        c->approx[point] += value;
        c->put_send++;
        received_puts++;

        finish_game = (received_puts == params.m);
        char * msg = create_state_msg(c->approx, params.k);
        eqPush(&c->q, now + c->delay, msg, true);
        free(msg); 
    }
}

void read_next_coeffs(client_t *c, FILE *fp) {
    size_t max_line = 6 + (params.n + 1) * 12 + 3;// tu zrob define
    char line[max_line];

    // The task mentioned that such a situation would never occur unless something new was written to the file
    // if my program encounters EOF it means that something new is already in the file so I keep trying to read it.
    while (!fgets(line, sizeof(line), fp)) {
        if (feof(fp)) {
            error("Unexpected EOF while reading COEFF");
            clearerr(fp);
            fseek(fp, 0, SEEK_CUR);
            sleep(1);
        }
        else {
            fatal("error while reading file");
        }
    }

    eqPush(&c->q, now_ms(), line, true);
    // It is not exact moment of sending COEFF, but on our lab it was mentioned that We can mark
    // something as sent when it is being put in the sending buffor.
    c->send_coeffs = true;

    size_t idx = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(line + 6, " \r\n", &saveptr);
         tok;
         tok = strtok_r(NULL, " ", &saveptr)) {
         c->coeffs[idx++] = strtod(tok, NULL);
    }
}

ssize_t process_message(client_t *c, FILE *fp) {
    size_t len;
    size_t cap = 0;
    char *line = NULL;

    while (get_line(&c->in_buf, "\r\n", 2, &line, &cap, &len) && !finish_game) {
        if (!c->received_hello) {
            if (strncmp(line, "HELLO ", 6) == 0 && is_valid_player_id(line + 6)) {
                c->player_id = strdup(line + 6);
                if (!c->player_id) fatal("Out of memory");
                c->received_hello = true;

                c->delay = count_lowercase(c->player_id) * 1000;

                printf("[%s]:%hu is now known as %s.\n", c->ipstr, c->port, c->player_id);
                read_next_coeffs(c, fp);
            }
            else {
                error_msg(c->ipstr, c->player_id, c->port, line);
                return -1;
            }
        }
        else {
            char *point_str, *value_str;
            if (strncmp(line, "PUT ", 4) == 0 && is_valid_put(line + 4,len - 4,&point_str, &value_str)) {
                process_put(c, point_str, value_str);
                printf("%s puts %s in %s\n", c->player_id, value_str, point_str);
            }
            else {             
                error_msg(c->ipstr, c->player_id, c->port, line);
            }
        }
    }
    free(line);
    return 1;
}

// This function removes all client who did not send hello and sets POLLOUT event when messages are ready to be sent.
void clean_up(struct pollfd *fds, client_t* clients) {
    uint64_t now = now_ms();
    for (int i = active_clients - 1; i >= 0; --i) {

        client_t *c = &clients[i];
        if (!c->received_hello && now > c->hello_deadline) {
            printf("ending connection - no hello (%d)\n",  i);
            end_connection(clients, fds, i);
            continue;
        }

        if (!eqEmpty(&c->q) && eqPeek(&c->q)->send_time <= now) {
            fds[i + 2].events = POLLIN | POLLOUT;
        }
        else {
            fds[i + 2].events = POLLIN;
        }
    }
}

void close_all(struct pollfd *fds, client_t* clients) {
    if (fds[0].fd >= 0) {
        close(fds[0].fd);
    }
    if (fds[1].fd >= 0) {
        close(fds[1].fd);
    }
    for (int i = active_clients - 1; i >= 0; --i) {
        end_connection(clients, fds, i);
    }
}

/* Termination signal handling. */
static void catch_int() {
    finish = true;
}

int main(int argc, char *argv[]) {

    read_params_server(argc, argv, &params);

    FILE *fp = fopen(params.file, "r");
    if (!fp) {
        syserr("fopen");
    }

    install_signal_handler(SIGINT, catch_int, SA_RESTART);

    int socket_ipv4 = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ipv4 < 0) {
        syserr("cannot create a socket");
    }

    // Bind the socket to a concrete address.
    struct sockaddr_in server_addr_ipv4;
    server_addr_ipv4.sin_family = AF_INET; // IPv4
    server_addr_ipv4.sin_addr.s_addr = htonl(INADDR_ANY); // Listening on all interfaces.
    server_addr_ipv4.sin_port = htons(params.port);

    int yes = 1;
    if (setsockopt(socket_ipv4, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        syserr("setsockopt SO_REUSEADDR");
    }

    if (bind(socket_ipv4, (struct sockaddr *) &server_addr_ipv4, (socklen_t) sizeof server_addr_ipv4) < 0) {
        syserr("bind");
    }

    if (listen(socket_ipv4, SOMAXCONN) < 0) {
        syserr("listen");
    }

    int socket_ipv6 = socket(AF_INET6, SOCK_STREAM, 0);
    if (socket_ipv6 < 0) {
        if (errno == EAFNOSUPPORT) {
            error("not supported protocol ipv6");
            socket_ipv6 = -1; 
        }
        else {
            syserr("cannot create a socket");
        }
    }
    else {
        int on = 1;
        if (setsockopt(socket_ipv6, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0) {
            syserr("setsockopt IPV6_V6ONLY");
        }

        int yes6 = 1;
        if (setsockopt(socket_ipv6, SOL_SOCKET, SO_REUSEADDR, &yes6, sizeof(yes6)) < 0) {
            syserr("setsockopt SO_REUSEADDR (IPv6)");
        }

        struct sockaddr_in6 server_addr_ipv6;
        server_addr_ipv6.sin6_family = AF_INET6;  // IPv6
        server_addr_ipv6.sin6_addr = in6addr_any; // Listening on all interfaces.
        server_addr_ipv6.sin6_port = server_addr_ipv4.sin_port;

        if (bind(socket_ipv6, (struct sockaddr *) &server_addr_ipv6 ,(socklen_t) sizeof server_addr_ipv6) < 0) {
            syserr("bind");
        }

        // Switch the socket to listening.
        if (listen(socket_ipv6, SOMAXCONN) < 0) {
            syserr("listen");
        }

    }

    client_t clients[CONNECTIONS - 2];

    struct pollfd poll_descriptors[CONNECTIONS];

    // The main socket has index 0 and 1.
    poll_descriptors[0].fd = socket_ipv4;
    poll_descriptors[0].events = POLLIN;

    poll_descriptors[1].fd = socket_ipv6;
    if (socket_ipv6 >= 0) {
        poll_descriptors[1].events = POLLIN;
    }
    else {
        poll_descriptors[1].events = 0;
    }

    for (int i = 2; i < CONNECTIONS; ++i) {
        poll_descriptors[i].fd = -1;
    }

    struct sockaddr_in client_addr_ipv4;
    struct sockaddr_in6 client_addr_ipv6;

    // Main loop.
    do {
        int poll_status = poll(poll_descriptors, active_clients + 2, TIMEOUT);
        if (poll_status == -1 ) {
            if (errno == EINTR) {
                continue;
            }
            else {
                syserr("poll");
            }
        }
        else if (poll_status > 0) {
            if (!finish && (poll_descriptors[0].revents & POLLIN)) {
                // New connection: new client is accepted.
                int client_fd = accept(poll_descriptors[0].fd,
                                       (struct sockaddr *) &client_addr_ipv4,
                                       &((socklen_t) {sizeof client_addr_ipv4}));
                if (client_fd < 0) {
                    syserr("accept");
                }

                if (!find_slot(poll_descriptors, clients, client_fd, (struct sockaddr *) &client_addr_ipv4)) {
                    close(client_fd);
                    printf("too many clients\n");
                }
            }
            if (!finish && (poll_descriptors[1].revents & POLLIN)) {
                int client_fd = accept(poll_descriptors[1].fd,
                    (struct sockaddr *) &client_addr_ipv6,
                    &((socklen_t) {sizeof client_addr_ipv6}));

                if (client_fd < 0) {
                    syserr("accept");
                }

                if (!find_slot(poll_descriptors, clients, client_fd, (struct sockaddr *) &client_addr_ipv6)) {
                    close(client_fd);
                    printf("too many clients\n");
                }
            }
            // Serve data connections.

            for (int i = active_clients + 1; i >= 2; --i) {

                if ((poll_descriptors[i].revents & POLLOUT)) {
                    ssize_t send = process_data_to_send(&clients[i-2].q, poll_descriptors[i].fd, clients[i-2].player_id);
                    if (send == -1) {
                        error("write");
                        end_connection(clients, poll_descriptors, i - 2);
                    }
                }
                if ((poll_descriptors[i].revents & (POLLIN | POLLERR))) {
                    

                    ssize_t received_bytes = read_message(&clients[i - 2].in_buf, poll_descriptors[i].fd);
                    const char *pid = clients[i -2].player_id ? clients[i -2].player_id  : "UNKNOWN";

                    if (received_bytes == -1) {
                        error("error when reading message from %s", pid);
                        end_connection(clients, poll_descriptors, i - 2);
                    } else if (received_bytes == 0) {
                        printf("ending connection with %s\n", pid);
                        end_connection(clients, poll_descriptors, i - 2);
                    } else if (received_bytes > 0) {
                        if (process_message(&clients[i - 2], fp) < 0) {
                            end_connection(clients, poll_descriptors, i - 2);
                            printf("ending connection with %s\n", pid);
                        }
                    }
                }
                if (finish_game) {
                    end_game(poll_descriptors, clients);
                    break;
                }
            }
        }
        clean_up(poll_descriptors, clients);

    } while (!finish);

    close_all(poll_descriptors, clients);
    
    fclose(fp);
    return 0;
}
