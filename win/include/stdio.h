/* Minimal stdio.h for msvcrt.dll (Windows build without an SDK). */
#ifndef W_STDIO_H
#define W_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct { char _opaque[48]; } FILE;   /* msvcrt's struct _iobuf on x64 */
FILE *__iob_func(void);
#define stdin  (&__iob_func()[0])
#define stdout (&__iob_func()[1])
#define stderr (&__iob_func()[2])
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
FILE *fopen(const char *, const char *);
FILE *_wfopen(const wchar_t *, const wchar_t *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fclose(FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int fflush(FILE *);
int fputc(int, FILE *);
char *fgets(char *, int, FILE *);
int fprintf(FILE *, const char *, ...);
int printf(const char *, ...);
int sscanf(const char *, const char *, ...);
int _snprintf(char *, size_t, const char *, ...);
int _vsnprintf(char *, size_t, const char *, va_list);
int _snwprintf(wchar_t *, size_t, const wchar_t *, ...);
static inline int snprintf(char *b, size_t n, const char *f, ...)
{ va_list a; va_start(a, f); int r = _vsnprintf(b, n, f, a); va_end(a); if (n) b[n - 1] = 0; return r; }
void perror(const char *);
#endif
