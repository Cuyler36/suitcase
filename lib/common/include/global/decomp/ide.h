#ifndef GLOBAL_HEADERS_DECOMP_SHUT_UP_IDE_H
#define GLOBAL_HEADERS_DECOMP_SHUT_UP_IDE_H

#if TARGET_PC

// The PC port really compiles this code: keep attributes and use the host intrinsics
#include <cw_compat.h>

#elif !defined(__MWERKS__)

#define asm
#define register
#define __attribute__(x)
#define __declspec(x)

extern void         __sync();
extern unsigned int __cntlzw(unsigned int value);
extern void         __memclr(void* buffer, int len);
extern unsigned int __rlwimi(unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e);
extern void*        __alloca(unsigned int blockSize);
extern int          __abs(int n);

#endif

#endif // GLOBAL_HEADERS_DECOMP_SHUT_UP_IDE_H
