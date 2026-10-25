/*
 * setjmp/longjmp for the Windows build, in assembly (win/setjmp_x64.c).
 * Saves every register the Win64 ABI calls non-volatile, xmm6-xmm15
 * included, and does no SEH unwinding. (clang's __builtin_setjmp restores
 * xmm6-xmm15 from the wrong place on Win64, which corrupted the caller's
 * registers after every frame.)
 */
#ifndef W_SETJMP_H
#define W_SETJMP_H
typedef unsigned long long jmp_buf[30];
int  __attribute__((returns_twice)) ps4_setjmp(jmp_buf b);
void __attribute__((noreturn)) ps4_longjmp(jmp_buf b, int v);
#define setjmp(b) ps4_setjmp(b)
#define longjmp(b, v) ps4_longjmp(b, v)
#endif
