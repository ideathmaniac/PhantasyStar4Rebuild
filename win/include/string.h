#ifndef W_STRING_H
#define W_STRING_H
#include <stddef.h>
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strchr(const char *, int);
char *_strdup(const char *);
#define strdup _strdup
size_t strlen(const char *);
char *strncpy(char *, const char *, size_t);
size_t wcslen(const wchar_t *);
wchar_t *wcsstr(const wchar_t *, const wchar_t *);
char *strstr(const char *, const char *);
wchar_t *wcscpy(wchar_t *, const wchar_t *);
wchar_t *wcscat(wchar_t *, const wchar_t *);
wchar_t *wcsrchr(const wchar_t *, wchar_t);
int _wcsicmp(const wchar_t *, const wchar_t *);
#endif
