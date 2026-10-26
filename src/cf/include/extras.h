#pragma once
// MSL's non-standard string functions (extras.h). MSVC's CRT has them; elsewhere they are
// implemented in src/cf/extras.c, as Dusklight does.
#include <stddef.h>
#include <string.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(_MSC_VER)
int stricmp(const char* a, const char* b);
int strnicmp(const char* a, const char* b, size_t n);
#endif
// UTF-16 text (wchar16_t: char16_t in C++, unsigned short in C, the same in memory)
#ifdef __cplusplus
int __msl_wcsnicmp(const char16_t* a, const char16_t* b, size_t n);
#else
int __msl_wcsnicmp(const unsigned short* a, const unsigned short* b, size_t n);
#endif

#ifdef __cplusplus
}
#endif
