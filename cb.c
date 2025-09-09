#define _GNU_SOURCE
#include "cb.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdio.h>
#include "err.h"

static const size_t CB_INITIAL_SIZE = 5000;

void cbInit(CircularBuffer *b)
{
  b->buf = malloc(CB_INITIAL_SIZE);
  if (!b->buf)
    fatal("Out of memory.");
  b->pos = 0;
  b->capacity = CB_INITIAL_SIZE;
  b->size = 0;
}

void cbDestroy(CircularBuffer *b)
{
  if (b->buf) {
    free(b->buf);
    b->buf = NULL;
  }
  b->size = 0;
  b->capacity = 0;
  b->pos = 0;
}

bool cbEmpty(CircularBuffer *b) {
  return (b->size == 0);
}

void cbPushBack(CircularBuffer *b, char const *data, size_t n)
{
  if (n + b->size > b->capacity) {
    size_t oldCapacity = b->capacity;
    size_t cm = 2;
    while (n + b->size > cm * b->capacity)
      cm *= 2;
    void *newBuf = reallocarray(b->buf, cm, b->capacity);
    if (!newBuf)
      fatal("Out of memory.");
    b->buf = newBuf;
    b->capacity = cm * oldCapacity;

    if (b->pos + b->size > oldCapacity)
      memmove(b->buf + oldCapacity, b->buf, b->pos + b->size - oldCapacity);
    if (b->pos) {
      memmove(b->buf, b->buf + b->pos, b->size);
      b->pos = 0;
    }
  }

  char *dataEnd = b->buf + ((b->pos + b->size) % b->capacity);
  size_t spaceAtEnd = b->capacity - (dataEnd - b->buf);
  memcpy(dataEnd, data, n > spaceAtEnd ? spaceAtEnd : n);
  if (n > spaceAtEnd)
    memcpy(b->buf, data + spaceAtEnd, n - spaceAtEnd);
  b->size += n;
}

void cbDropFront(CircularBuffer *b, size_t n)
{
  assert(n <= b->size);

  b->pos = (b->pos + n) % b->capacity;
  b->size -= n;
}

size_t cbGetContinuousCount(CircularBuffer const *b)
{
  size_t possible = b->capacity - b->pos;
  return possible > b->size ? b->size : possible;
}

size_t cbGetLineLen(CircularBuffer const *b, const char *term, size_t term_len) {
  size_t first_chunk = cbGetContinuousCount(b);
  const char *start = cbGetData(b);

  char *pos = memmem(start, first_chunk, term, term_len);
  if (pos) {
      return (size_t)(pos - start) + term_len;
  }

  size_t wrapped = b->size - first_chunk;
  if (wrapped >= 1) {

      if (term_len == 2) {
        if (start[first_chunk - 1] == term[0] && b->buf[0] == term[1]) {
          return first_chunk + 1;
        }
      }
      char *pos2 = memmem(b->buf, wrapped, term, term_len);
      if (pos2) {
          return first_chunk + (pos2 - b->buf) + term_len;
      }
  }

  // term to moze byc albo \n albo \r\n wiec musimy jeszcze edge case ogarnac.

  return 0;
}

size_t cbGetLine(CircularBuffer *b, char *out_line, const char* term, size_t term_len, size_t max_len) {
  size_t total_len = cbGetLineLen(b, term, term_len);

  if (total_len == 0) {
      return 0;
  }
  if (total_len - term_len + 1 > max_len) {
      return 0;
  }

  size_t content_len = total_len - term_len;

  size_t first_chunk = cbGetContinuousCount(b);
  const char *data = cbGetData(b);
  if (content_len <= first_chunk) {
      memcpy(out_line, data, content_len);
  } else {
      //printf("hree %ld\n", first_chunk);
      memcpy(out_line, data, first_chunk);
      memcpy(out_line + first_chunk, b->buf, content_len - first_chunk);
  }
  out_line[content_len] = '\0';

  cbDropFront(b, total_len);
  return content_len;
}

char *cbGetData(CircularBuffer const *b)
{
  return b->buf + b->pos;
}
