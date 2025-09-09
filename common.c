#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

#include "err.h"
#include "common.h"
#include "messages.h"

// read_size zostaw usun swoja poprzednia funkcje 

uint16_t read_port(char const *string) {
    char *endptr;
    errno = 0;
    unsigned long port = strtoul(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number", string);
    }
    return (uint16_t) port;
}

size_t read_size(char const *string, unsigned long min, unsigned long max, char const *name) {
    char *endptr;
    errno = 0;
    unsigned long long number = strtoull(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || number > max || number < min) {
        fatal("Invalid value %s: %s \n", name, string);
    }
    return number;
}

struct sockaddr_in get_server_addr_ipv4(char const *host, uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in send_address;
    send_address.sin_family = AF_INET;   // IPv4
    send_address.sin_addr.s_addr =       // IP address
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr;
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
}

struct sockaddr_in6 get_server_addr_ipv6(char const *host, uint16_t port){
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET6; // IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in6 server_address;
    server_address.sin6_family = AF_INET6;  // IPv6
    server_address.sin6_addr =
            ((struct sockaddr_in6 *) (address_result->ai_addr))->sin6_addr;
    server_address.sin6_port = htons(port);

    freeaddrinfo(address_result);

    return server_address;
}

void install_signal_handler(int signal, void (*handler)(int), int flags) {
    struct sigaction action;
    sigset_t block_mask;

    sigemptyset(&block_mask);
    action.sa_handler = handler;
    action.sa_mask = block_mask;
    action.sa_flags = flags;

    if (sigaction(signal, &action, NULL) < 0) {
        syserr("sigaction");
    }
}

void get_protocol(client_params *params) {
    struct addrinfo hints = {0}, *res;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        int err = getaddrinfo(params->server_addr, NULL, &hints, &res);
        if (err != 0) {
            fatal("getaddrinfo: %s", gai_strerror(err));
        }

        params->ipv4 = false;
        params->ipv6 = false;
        if (res->ai_family == AF_INET) {
            params->ipv4 = true;
        }
        else if (res->ai_family == AF_INET6) {
            params->ipv6 = true;
        }
        else {
            fatal("nieobsługiwane ai_family: %d", res->ai_family);
        }

        freeaddrinfo(res);
}


void read_params_server(int argc, char *argv[], server_params *params) {
    bool f_set = false, k_set = false, p_set = false, n_set = false, m_set = false;

    params->port = 0;
    params->k = 100;
    params->n = 4;
    params->m = 131;

    // Reading params.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-f") == 0 && (i + 1 < argc) && !f_set) {
            params->file = argv[++i];
            f_set = true;
        }
        else if (strcmp(argv[i], "-k") == 0 && (i + 1 < argc) && !k_set) {
            params->k = read_size(argv[++i], 1, MAX_K, "K");
            k_set = true;
        }
        else if (strcmp(argv[i], "-p") == 0 && (i + 1 < argc) && !p_set) {
            params->port = read_port(argv[++i]);
            p_set = true;
        }
        else if (strcmp(argv[i], "-n") == 0 && (i + 1 < argc) && !n_set) {
            params->n = read_size(argv[++i], 1, MAX_N, "N");
            n_set = true;
        }
        else if (strcmp(argv[i], "-m") == 0 && (i + 1 < argc) && !m_set) {
            params->m = read_size(argv[++i], 1, MAX_M, "M");
            m_set = true;
        }
        else {
            fatal("invalid parameter: %s ", argv[i]);
        }
    }

    if (!f_set) {
        fatal("no parameter f");
    }

}

void read_params_client(int argc, char *argv[], client_params *params) {
    bool u_set = false, s_set = false, p_set = false;

    params->ipv4 = false;
    params->ipv6 = false;
    params->a = false;

    // Reading params.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-s") == 0 && (i + 1 < argc) && !s_set) {
            params->server_addr = argv[++i];
            s_set = true;
        }
        else if (strcmp(argv[i], "-a") == 0  && !params->a) {
            params->a = true;
        }
        else if (strcmp(argv[i], "-p") == 0 && (i + 1 < argc) && !p_set) {
            params->port = read_port(argv[++i]);
            if (params->port == 0) {
                fatal("Port can't be 0");
            }
            p_set = true;
        }
        else if (strcmp(argv[i], "-u") == 0 && (i + 1 < argc) && !u_set) {
            params->id = argv[++i];
            if (!is_valid_player_id(params->id)) {
                fatal("invalid player id");
            }
            u_set = true;
        }
        else if (strcmp(argv[i], "-4") == 0  && !params->ipv4) {
            params->ipv4 = true;
        }
        else if (strcmp(argv[i], "-6") == 0  && !params->ipv6) {
            params->ipv6 = true;
        }
        else {
            fatal("invalid parameter: %s ", argv[i]);
        }
    }

    if (!p_set || !s_set || !u_set) {
        fatal("Options -p, -u, -s must be used.");
    }

    if ((params->ipv4 && params->ipv6) || (!params->ipv4 && !params->ipv6)) {
        get_protocol(params);
    }
}

uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}