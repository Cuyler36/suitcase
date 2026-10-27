// Host implementations of the CodeWarrior / PowerPC intrinsics declared in include/lib/cw_compat.h.
#include <cw_compat.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

// PowerPC numbers bits from the MSB: mask bits mb..me inclusive, wrapping when mb > me
static uint32_t ppc_mask(int mb, int me) {
    uint32_t begin = 0xFFFFFFFFu >> (mb & 31);
    uint32_t end = 0xFFFFFFFFu << (31 - (me & 31));
    return (mb & 31) <= (me & 31) ? (begin & end) : (begin | end);
}

static uint32_t rotl32(uint32_t value, int shift) {
    shift &= 31;
    return shift == 0 ? value : (value << shift) | (value >> (32 - shift));
}

int __abs(int n) {
    return abs(n);
}

long __labs(long n) {
    return labs(n);
}

int __cntlzw(unsigned int value) {
    if (value == 0) {
        return 32;  // cntlzw returns 32 for 0
    }
#ifdef _MSC_VER
    unsigned long idx;
    _BitScanReverse(&idx, value);
    return 31 - (int)idx;
#else
    return __builtin_clz(value);
#endif
}

int __rlwimi(int a, int b, int shift, int mb, int me) {
    uint32_t mask = ppc_mask(mb, me);
    return (int)((rotl32((uint32_t)b, shift) & mask) | ((uint32_t)a & ~mask));
}

int __rlwinm(int value, int shift, int mb, int me) {
    return (int)(rotl32((uint32_t)value, shift) & ppc_mask(mb, me));
}

int __rlwnm(int value, int shift, int mb, int me) {
    return __rlwinm(value, shift & 31, mb, me);
}

// PowerPC's divw and divwu do not trap: a zero divisor (or INT_MIN / -1) gives what the Wii's CPU
// gives, as Dolphin emulates it (-1 for a negative dividend, otherwise 0). x86 raises an exception,
// so the game's divisions by a value that can be zero go through these (../pc_port_ub.md UB-19).
int __divw(int a, int b) {
    if (b == 0 || (a == INT32_MIN && b == -1)) {
        return a < 0 ? -1 : 0;
    }
    return a / b;
}

unsigned int __divwu(unsigned int a, unsigned int b) {
    if (b == 0) {
        return 0;
    }
    return a / b;
}

int __mulhw(int a, int b) {
    return (int)(((int64_t)a * (int64_t)b) >> 32);
}

unsigned int __mulhwu(unsigned int a, unsigned int b) {
    return (unsigned int)(((uint64_t)a * (uint64_t)b) >> 32);
}

// The byte-reversed forms read and write little-endian values, which is native order on the host
int __lhbrx(const void* base, int offset) {
    uint8_t bytes[2];
    memcpy(bytes, (const uint8_t*)base + offset, sizeof(bytes));
    return bytes[0] | (bytes[1] << 8);
}

int __lwbrx(const void* base, int offset) {
    uint8_t bytes[4];
    memcpy(bytes, (const uint8_t*)base + offset, sizeof(bytes));
    return (int)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
                 ((uint32_t)bytes[3] << 24));
}

void __sthbrx(unsigned short value, void* base, int offset) {
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    memcpy((uint8_t*)base + offset, bytes, sizeof(bytes));
}

void __stwbrx(unsigned int value, void* base, int offset) {
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    memcpy((uint8_t*)base + offset, bytes, sizeof(bytes));
}

double __fabs(double x) {
    return fabs(x);
}

float __fabsf(float x) {
    return fabsf(x);
}

double __fnabs(double x) {
    return -fabs(x);
}

float __fnabsf(float x) {
    return -fabsf(x);
}

double __fmadd(double a, double c, double b) {
    return a * c + b;
}

float __fmadds(float a, float c, float b) {
    return a * c + b;
}

double __fmsub(double a, double c, double b) {
    return a * c - b;
}

float __fmsubs(float a, float c, float b) {
    return a * c - b;
}

double __fnmadd(double a, double c, double b) {
    return -(a * c + b);
}

float __fnmadds(float a, float c, float b) {
    return -(a * c + b);
}

double __fnmsub(double a, double c, double b) {
    return -(a * c - b);
}

float __fnmsubs(float a, float c, float b) {
    return -(a * c - b);
}

// fres/frsqrte are hardware estimates; the exact value is a valid estimate
float __fres(float x) {
    return 1.0f / x;
}

double __frsqrte(double x) {
    return 1.0 / sqrt(x);
}

double __fsel(double a, double b, double c) {
    return a >= 0.0 ? b : c;
}

float __fsels(float a, float b, float c) {
    return a >= 0.0f ? b : c;
}

// No FPSCR on the host: report the default state and ignore writes
double __mffs(void) {
    return 0.0;
}

double __setflm(double fpscr) {
    (void)fpscr;
    return 0.0;
}

void __dcbf(void* base, int offset) {
    (void)base;
    (void)offset;
}

void __dcbst(void* base, int offset) {
    (void)base;
    (void)offset;
}

void __dcbt(void* base, int offset) {
    (void)base;
    (void)offset;
}

void __dcbtst(void* base, int offset) {
    (void)base;
    (void)offset;
}

// dcbz zeroes the whole 32-byte cache line containing the address
void __dcbz(void* base, int offset) {
    uintptr_t line = ((uintptr_t)base + (intptr_t)offset) & ~(uintptr_t)31;
    memset((void*)line, 0, 32);
}

void __eieio(void) {}

void __isync(void) {}

void __sync(void) {}

char* __strcpy(char* dst, const char* src) {
    return strcpy(dst, src);
}
