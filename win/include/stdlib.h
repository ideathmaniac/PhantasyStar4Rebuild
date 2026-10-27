#ifndef W_STDLIB_H
#define W_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
char *getenv(const char *);
int atoi(const char *);
int abs(int);
long atol(const char *);
void exit(int);
void abort(void);
#endif
