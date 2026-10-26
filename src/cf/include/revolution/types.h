#ifndef REVOLUTION_TYPES_H
#define REVOLUTION_TYPES_H
// PC: aurora's base types (identical to include/types.h on PC) plus the RVL additions of the
// decomp's include/lib/revolution/types.h.
#include <dolphin/types.h>
#include <decomp.h>

#if defined(_MSC_VER) && !defined(__clang__)
// aurora's MSVC ATTRIBUTE_ALIGN is __declspec(align), which MSVC rejects after the declarator, where
// the decomp puts it. aurora's headers do not use the macro, so dropping it changes no aurora layout.
#undef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(num)
#endif

typedef unsigned int uBOOL;
typedef volatile BOOL vBOOL;
typedef volatile uBOOL vuBOOL;

#define DEFAULT_ALIGN 32
#define ALIGN32 ATTRIBUTE_ALIGN(DEFAULT_ALIGN)
#define ALIGN64 ATTRIBUTE_ALIGN(64)

#if defined(__GNUC__) || defined(__clang__)
#define PACKED __attribute__((packed))
#else
#define PACKED
#endif

// UTF-16 text, as in include/types.h (same guard): char16_t in C++, unsigned short in C
#ifndef DECOMP_WCHAR16_DEFINED
#define DECOMP_WCHAR16_DEFINED
#ifdef __cplusplus
typedef char16_t wchar16_t;
#else
typedef unsigned short wchar16_t;
#endif
#define WSTR(s) u##s
#endif
#ifdef __cplusplus
#include <port/wchar16.h>
#endif

#endif // REVOLUTION_TYPES_H
