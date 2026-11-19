#ifndef _FREESTANDING_STDLIB_H
#define _FREESTANDING_STDLIB_H

#include <stddef.h>

static inline int abs(int x) { return x < 0 ? -x : x; }
void *malloc(size_t size);
void free(void *ptr);
char *itoa(int value, char *str, int base);

#endif
