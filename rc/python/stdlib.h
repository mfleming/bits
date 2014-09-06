#ifndef STDLIB_H
#define STDLIB_H

#include <sys/types.h>

__attribute__((noreturn)) void abort(void);

void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);

#endif /* STDLIB_H */
