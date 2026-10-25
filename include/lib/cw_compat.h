/**
 * CodeWarrior / PowerPC intrinsics for the PC port (TARGET_PC only).
 *
 * MWCC provides these as builtins. On PC they are ordinary functions implemented in
 * src/cf/cw_compat.c (as Dusklight does in include/global.h and src/dusk/extras.c), except
 * __alloca and __memclr, which must expand in the caller. Included by the decomp's base
 * headers (include/decomp/compat.h, lib/common/include/global/decomp/ide.h) under TARGET_PC.
 */

#ifndef CW_COMPAT_H
#define CW_COMPAT_H

#if TARGET_PC

#include <string.h>

#if defined(_MSC_VER)
#include <malloc.h>
#define __alloca _alloca
#else
#define __alloca __builtin_alloca
#endif
#define __memclr(dst, size) memset((dst), 0, (size))

#ifdef __cplusplus
extern "C" {
#endif

// Integer
int __abs(int n);
long __labs(long n);
int __cntlzw(unsigned int value);
int __rlwimi(int a, int b, int shift, int mb, int me);
int __rlwinm(int value, int shift, int mb, int me);
int __rlwnm(int value, int shift, int mb, int me);
int __divw(int a, int b);
unsigned int __divwu(unsigned int a, unsigned int b);
int __mulhw(int a, int b);
unsigned int __mulhwu(unsigned int a, unsigned int b);

// Byte-reversed loads and stores (little-endian access)
int __lhbrx(const void* base, int offset);
int __lwbrx(const void* base, int offset);
void __sthbrx(unsigned short value, void* base, int offset);
void __stwbrx(unsigned int value, void* base, int offset);

// Floating point
double __fabs(double x);
float __fabsf(float x);
double __fnabs(double x);
float __fnabsf(float x);
double __fmadd(double a, double c, double b);
float __fmadds(float a, float c, float b);
double __fmsub(double a, double c, double b);
float __fmsubs(float a, float c, float b);
double __fnmadd(double a, double c, double b);
float __fnmadds(float a, float c, float b);
double __fnmsub(double a, double c, double b);
float __fnmsubs(float a, float c, float b);
float __fres(float x);
double __frsqrte(double x);
double __fsel(double a, double b, double c);
float __fsels(float a, float b, float c);
double __mffs(void);
double __setflm(double fpscr);

// Cache and synchronization (no-ops on the host, except __dcbz which clears a 32-byte line)
void __dcbf(void* base, int offset);
void __dcbst(void* base, int offset);
void __dcbt(void* base, int offset);
void __dcbtst(void* base, int offset);
void __dcbz(void* base, int offset);
void __eieio(void);
void __isync(void);
void __sync(void);

// String
char* __strcpy(char* dst, const char* src);

#ifdef __cplusplus
}
#endif

#endif // TARGET_PC

#endif // CW_COMPAT_H
