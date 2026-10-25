/* setjmp/longjmp via the compiler builtins (no CRT unwinding needed). */
#ifndef W_SETJMP_H
#define W_SETJMP_H
typedef void *jmp_buf[5];
#define setjmp(b) __builtin_setjmp(b)
#define longjmp(b, v) __builtin_longjmp(b, 1)
#endif
