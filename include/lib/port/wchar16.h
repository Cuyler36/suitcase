// The C library's wide-character functions for wchar16_t (char16_t) text on PC (the PC port). The game's
// text is UTF-16 (wchar_t on the Wii); on PC it is char16_t, which the host's wcs*/wmem*/w*printf do
// not take. Included by types.h in C++ on PC.
#ifndef PORT_WCHAR16_H
#define PORT_WCHAR16_H

#if TARGET_PC && defined(__cplusplus)

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

extern "C++" {

inline size_t wcslen(const char16_t* s) {
    const char16_t* p = s;
    while (*p != 0) {
        p++;
    }
    return (size_t)(p - s);
}

inline char16_t* wcscpy(char16_t* dst, const char16_t* src) {
    char16_t* d = dst;
    while ((*d++ = *src++) != 0) {
    }
    return dst;
}

inline char16_t* wcsncpy(char16_t* dst, const char16_t* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != 0; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = 0;
    }
    return dst;
}

inline char16_t* wcscat(char16_t* dst, const char16_t* src) {
    wcscpy(dst + wcslen(dst), src);
    return dst;
}

inline char16_t* wcsncat(char16_t* dst, const char16_t* src, size_t n) {
    char16_t* d = dst + wcslen(dst);
    size_t i = 0;
    for (; i < n && src[i] != 0; i++) {
        d[i] = src[i];
    }
    d[i] = 0;
    return dst;
}

inline int wcscmp(const char16_t* a, const char16_t* b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

inline int wcsncmp(const char16_t* a, const char16_t* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == 0) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

inline char16_t* wcschr(char16_t* s, char16_t c) {
    for (;; s++) {
        if (*s == c) {
            return s;
        }
        if (*s == 0) {
            return nullptr;
        }
    }
}

inline const char16_t* wcschr(const char16_t* s, char16_t c) {
    return wcschr(const_cast<char16_t*>(s), c);
}

inline char16_t* wmemset(char16_t* s, char16_t c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        s[i] = c;
    }
    return s;
}

inline char16_t* wmemcpy(char16_t* dst, const char16_t* src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
    return dst;
}

inline const char16_t* wmemchr(const char16_t* s, char16_t c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == c) {
            return s + i;
        }
    }
    return nullptr;
}

// UTF-16 to UTF-8; returns the bytes written (not counting the terminator), or (size_t)-1
inline size_t wcstombs(char* dst, const char16_t* src, size_t n) {
    size_t out = 0;
    for (; *src != 0; src++) {
        u32 c = *src;
        if (c >= 0xD800 && c < 0xDC00 && src[1] >= 0xDC00 && src[1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (src[1] - 0xDC00);
            src++;
        }
        char buf[4];
        size_t len;
        if (c < 0x80) {
            buf[0] = (char)c;
            len = 1;
        } else if (c < 0x800) {
            buf[0] = (char)(0xC0 | (c >> 6));
            buf[1] = (char)(0x80 | (c & 0x3F));
            len = 2;
        } else if (c < 0x10000) {
            buf[0] = (char)(0xE0 | (c >> 12));
            buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (c & 0x3F));
            len = 3;
        } else {
            buf[0] = (char)(0xF0 | (c >> 18));
            buf[1] = (char)(0x80 | ((c >> 12) & 0x3F));
            buf[2] = (char)(0x80 | ((c >> 6) & 0x3F));
            buf[3] = (char)(0x80 | (c & 0x3F));
            len = 4;
        }
        if (dst != nullptr) {
            if (out + len > n) {
                break;
            }
            for (size_t i = 0; i < len; i++) {
                dst[out + i] = buf[i];
            }
        }
        out += len;
    }
    if (dst != nullptr && out < n) {
        dst[out] = 0;
    }
    return out;
}

// Formatting: forwarded to the host's wide printf, which needs a 16-bit wchar_t (Windows); other hosts
// need their own formatter. MSL's wide printf follows C99 (%s is a char string, %ls a wide one), so the
// UCRT is asked for its ISO specifiers here (a global _CRT_STDIO_ISO_WIDE_SPECIFIERS must match across
// every library in the link).
static_assert(sizeof(wchar_t) == sizeof(char16_t), "port/wchar16.h: the host wchar_t is not UTF-16");

#if defined(_MSC_VER)
#define PORT_WPRINTF_OPTIONS (_CRT_INTERNAL_LOCAL_PRINTF_OPTIONS & ~_CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS)
#endif

inline int vswprintf(char16_t* buf, size_t n, const char16_t* fmt, va_list args) {
#if defined(_MSC_VER)
    int r = __stdio_common_vswprintf(PORT_WPRINTF_OPTIONS, reinterpret_cast<wchar_t*>(buf), n,
                                     reinterpret_cast<const wchar_t*>(fmt), nullptr, args);
    return r < 0 ? -1 : r;
#else
    return ::vswprintf(reinterpret_cast<wchar_t*>(buf), n, reinterpret_cast<const wchar_t*>(fmt), args);
#endif
}

inline int swprintf(char16_t* buf, size_t n, const char16_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vswprintf(buf, n, fmt, args);
    va_end(args);
    return r;
}

inline int wprintf(const char16_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
#if defined(_MSC_VER)
    int r = __stdio_common_vfwprintf(PORT_WPRINTF_OPTIONS, stdout, reinterpret_cast<const wchar_t*>(fmt), nullptr,
                                     args);
#else
    int r = ::vwprintf(reinterpret_cast<const wchar_t*>(fmt), args);
#endif
    va_end(args);
    return r;
}

} // extern "C++"

// nw4r calls them through std:: (std::wcslen, std::vswprintf)
namespace std {
using ::vswprintf;
using ::wcslen;
} // namespace std

#endif

#endif // PORT_WCHAR16_H
