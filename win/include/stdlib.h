#ifndef W_STDLIB_H
#define W_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
char *getenv(const char *);
int atoi(const char *);
long atol(const char *);
void exit(int);
void abort(void);
#endif
