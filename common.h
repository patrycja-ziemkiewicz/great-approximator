#ifndef MIM_COMMON_H
#define MIM_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#define MAX_M 12341234
#define MAX_K 10000
#define MAX_N 8

// 1) Send uint16_t, int32_t etc., not int.
//    The length of int is platform-dependent.
// 2) If we want to send a structure, we have to declare it
//    with __attribute__((__packed__)). Otherwise the compiler
//    may add a padding bewteen fields. In the following example
//    sizeof (data_pkt) is then 8, not 6.

typedef struct {
    const char *file;
    uint16_t port;
    size_t k;
    size_t n;
    size_t m;
} server_params;

typedef struct {
    const char *id;
    const char *server_addr;
    uint16_t port;
    bool ipv4;
    bool ipv6;
    bool a;
} client_params;

typedef struct __attribute__((__packed__)) {
    uint16_t seq_no;
    uint32_t number;
} data_pkt;

typedef struct __attribute__((__packed__)) {
    uint64_t sum;
} response_pkt;


uint16_t read_port(char const *string);
size_t read_size(char const *string, unsigned long min, unsigned long max, char const *name);

struct sockaddr_in get_server_addr_ipv4(char const *host, uint16_t port);
struct sockaddr_in6 get_server_addr_ipv6(char const *host, uint16_t port);
void install_signal_handler(int signal, void (*handler)(int), int flags);

void read_params_server(int argc, char *argv[], server_params *params);
void read_params_client(int argc, char *argv[], client_params *params);

uint64_t now_ms(void);

#endif