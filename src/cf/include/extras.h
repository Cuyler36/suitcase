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
int __msl_wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n);

#ifdef __cplusplus
}
#endif
