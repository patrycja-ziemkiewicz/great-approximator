#ifndef __CBUFFER_H__
#define __CBUFFER_H__

#include <stddef.h>
#include <stdbool.h>

typedef struct CircularBuffer {
  char *buf;
  size_t pos;
  size_t capacity;
  size_t size;
} CircularBuffer;

void cbInit(CircularBuffer *b);
void cbDestroy(CircularBuffer *b);
bool cbEmpty(CircularBuffer *b);
void cbPushBack(CircularBuffer *b, char const *data, size_t n);
void cbDropFront(CircularBuffer *b, size_t n);
size_t cbGetContinuousCount(CircularBuffer const *b);
char *cbGetData(CircularBuffer const *b);
size_t cbGetLineLen(CircularBuffer const *b, const char *term, size_t term_len);
size_t cbGetLine(CircularBuffer *b, char *out_line, const char* term, size_t term_len, size_t max_len);

#endif
