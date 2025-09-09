#ifndef MIM_ERR_H
#define MIM_ERR_H

#include <stdnoreturn.h>
#include <inttypes.h>

// Print information about a system error and quits.
noreturn void syserr(const char* fmt, ...);

// Print information about an error and quits.
noreturn void fatal(const char* fmt, ...);

// Print information about an error and return.
void error(const char* fmt, ...);

void error_msg(char* ip,char* id, uint16_t port, char* line);

#endif
