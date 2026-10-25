#pragma once

#include <game_version.h>

// The original builds declared strcmp before MSL's string.h (most likely a
// precompiled/prefix header). MWCC only treats a strcmp first declared inside
// string.h / cstring / memory.h as the library function that reads but never
// writes memory (its stdlibfunccheck table), which lets loads be hoisted across
// the call. Units whose loops show the original's reloads define
// DECLARE_STRCMP_EARLY (configure.py) to get the plain declaration first:
// d_bgi_drawNP (dBgiDraw_c::getTexIdx).
#ifdef DECLARE_STRCMP_EARLY
#ifdef __cplusplus
extern "C" {
#endif
int strcmp(const char *, const char *);
#ifdef __cplusplus
}
#endif
#endif

#ifdef __cplusplus
#include <cstdarg>
#include <cstddef>
#include <new>
#else
#include <stdarg.h>
#include <stddef.h>
#endif

// Codewarrior-specific pragmas
#ifdef __CWCC__
#pragma cpp1x on
#pragma gcc_extensions on
#endif

// Base types
typedef unsigned char u8;
typedef unsigned char byte_t;
typedef unsigned short u16;
typedef unsigned short byte2_t;
#if TARGET_PC
// long is 64-bit on LP64 hosts and a distinct type from int on all of them; keep these 32-bit
// and identical to the SDK's u32/s32 so overrides and overloads still match.
typedef unsigned int u32;
#else
typedef unsigned long u32;
#endif
typedef unsigned int byte4_t;
typedef unsigned long long u64;
typedef signed char s8;
typedef signed short s16;
#if TARGET_PC
typedef signed int s32;
#else
typedef signed long s32;
#endif
typedef signed long long s64;
typedef float f32;
typedef double f64;
#if TARGET_PC
typedef u32 ulong;
#else
typedef unsigned long ulong;
#endif
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;

typedef int UNKWORD;
typedef void UNKTYPE;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
typedef int BOOL;

// Macros
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define ARRAY_MAX_STRLEN(a) (ARRAY_SIZE(a) - 1)
#define ARRAY_LAST(a) (a[ARRAY_SIZE(a) - 1])
#define BIT_FLAG(bit) ((bit) < 0 ? 0 : 1 << (bit))
#define ROUND_UP(x, align) (((x) + (align) - 1) & (-(align)))
#define ROUND_UP_PTR(x, align)                                                 \
    ((void *)((((u32)(x)) + (align) - 1) & (~((align) - 1))))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, min, max) ((v) < (min) ? (min) : ((v) > (max) ? (max) : (v)))

// No-op on release
#define EGG_ASSERT(...)
#define EGG_ASSERT_MSG(...)

#ifndef NULL
#define NULL 0
#endif // NULL

// nullptr is a keyword (not a macro) on the host compilers
#if !TARGET_PC
#ifndef nullptr
#define nullptr 0
#endif // nullptr
#endif

#if TARGET_PC
// No ELF sections on PC. MSVC has no weak definitions, as in Dusklight.
#define DECL_SECTION(x)
#if defined(__GNUC__) || defined(__clang__)
#define DECL_WEAK __attribute__((weak))
#else
#define DECL_WEAK
#endif
#else
#define DECL_SECTION(x) __declspec(section x)
#define DECL_WEAK __declspec(weak)
#endif
#define ATTR_UNUSED

#if defined(__CWCC__) && !defined(__INTELLISENSE__)
#define NOINLINE __attribute__((noinline))
#define ALIGN(x) __attribute__((aligned(x)))
#define DECOMP_INLINE inline
#define DECOMP_DONT_INLINE __attribute__((never_inline))
#define DONT_INLINE __attribute__((never_inline))
#define AT_ADDRESS(x) : x
#define override

// Static assert
#define GLUE(a, b) a##b
#define GLUE2(a, b) GLUE(a, b)
#define STATIC_ASSERT(cond)                                                    \
    typedef char GLUE2(static_assertion_failed, __LINE__)[(cond) ? 1 : -1]
#elif TARGET_PC
#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#define ALIGN(x) __attribute__((aligned(x)))
#else
#define NOINLINE __declspec(noinline)
#define ALIGN(x)
#endif
#define DECOMP_INLINE
#define DECOMP_DONT_INLINE
#define DONT_INLINE
#define AT_ADDRESS(x)
#define STATIC_ASSERT(...)
#else
#define NOINLINE
#define ALIGN(x)
#define DECOMP_INLINE
#define DECOMP_DONT_INLINE
#define DONT_INLINE
#define AT_ADDRESS(x)
#define STATIC_ASSERT(...)
#endif

#if !defined(__MWERKS__) && !defined(M_PI)
#define M_PI 3.141592653589793f
#endif

// C99 compound literals, e.g. COMPOUND_LITERAL(GXColor){r, g, b, a}. MSVC's C++ compiler has no
// compound literals, so there it becomes a braced initializer (as in Dusklight).
#if defined(_MSVC_LANG) && !defined(__clang__)
#define COMPOUND_LITERAL(x)
#else
#define COMPOUND_LITERAL(x) (x)
#endif

// Layout checks against the Wii's 32-bit struct sizes and offsets. They do not hold for 64-bit
// PC builds, so there they are always true.
#if TARGET_PC
#ifdef __cplusplus
#define STATIC_ASSERT_WII(name, cond) static_assert(true, #name)
#else
#define STATIC_ASSERT_WII(name, cond) _Static_assert(1, #name)
#endif
#else
#define STATIC_ASSERT_WII(name, cond) typedef char name[(cond) ? 1 : -1]
#endif

// The address of a temporary, e.g. f(TEMP_ADDR(mVec3_c(1.0f, 1.0f, 1.0f))). CW accepts &temporary;
// MSVC and clang do not. As there, the pointer is valid until the end of the full expression.
#if TARGET_PC && defined(__cplusplus)
#include <type_traits>
template <typename T>
inline std::remove_reference_t<T> *TempAddr(T &&value) {
    return &value;
}
#define TEMP_ADDR(x) TempAddr(x)
#else
#define TEMP_ADDR(x) (&(x))
#endif

// CodeWarrior's spelling of decltype (the decomp keeps the original; Dusklight maps the other way)
#if TARGET_PC && defined(__cplusplus)
#define __decltype__ decltype
#endif

// Shared helpers (from the nw4r import): string / FOURCC / rounding macros and
// the inline-asm compatibility layer.
#include <macros.h>
#include <decomp/compat.h>
#include <decomp/decomp.h>
#include <decomp/lang.h>
