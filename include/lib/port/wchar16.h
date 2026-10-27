// The C library's wide-character functions for wchar16_t (char16_t) text on PC (the PC port). The game's
// text is UTF-16 (wchar_t on the Wii); on PC it is char16_t, which the host's wcs*/wmem*/w*printf do
// not take. Included by types.h in C++ on PC.
#ifndef PORT_WCHAR16_H
#define PORT_WCHAR16_H

#if TARGET_PC && defined(__cplusplus)

extern "C++" {

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

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

// Formatting, as MSL's wide printf: C99 specifiers (%s is a char string, %ls a wide one). On Windows,
// where wchar_t is UTF-16, it is the UCRT's wide printf, asked for its ISO specifiers (a global
// _CRT_STDIO_ISO_WIDE_SPECIFIERS must match across every library in the link). Elsewhere wchar_t is
// UTF-32, so the conversions are formatted here: numbers by the host's snprintf, strings copied.
#if defined(_MSC_VER)
static_assert(sizeof(wchar_t) == sizeof(char16_t), "port/wchar16.h: the host wchar_t is not UTF-16");
#define PORT_WPRINTF_OPTIONS (_CRT_INTERNAL_LOCAL_PRINTF_OPTIONS & ~_CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS)
#else
namespace port_wchar16 {

// Appends to buf (capacity n, the terminator included) and counts what would have been written
struct Out {
    char16_t* buf;
    size_t n;
    size_t len;

    void put(char16_t c) {
        if (len + 1 < n) {
            buf[len] = c;
        }
        len++;
    }
    void pad(size_t count, char16_t c) {
        for (size_t i = 0; i < count; i++) {
            put(c);
        }
    }
};

inline int vformat(char16_t* buf, size_t n, const char16_t* fmt, va_list args) {
    Out out = {buf, n, 0};
    while (*fmt != 0) {
        if (*fmt != u'%') {
            out.put(*fmt++);
            continue;
        }
        const char16_t* start = fmt++;
        if (*fmt == u'%') {
            out.put(*fmt++);
            continue;
        }

        // The specifier, rebuilt as a char string for snprintf (* width and precision read here)
        char spec[32];
        size_t specLen = 0;
        spec[specLen++] = '%';
        bool left = false;
        while (*fmt == u'-' || *fmt == u'+' || *fmt == u' ' || *fmt == u'#' || *fmt == u'0') {
            left |= *fmt == u'-';
            spec[specLen++] = (char)*fmt++;
        }
        int width = -1;
        if (*fmt == u'*') {
            width = va_arg(args, int);
            if (width < 0) {
                left = true;
                width = -width;
                spec[specLen++] = '-';
            }
            fmt++;
        } else if (*fmt >= u'0' && *fmt <= u'9') {
            width = 0;
            while (*fmt >= u'0' && *fmt <= u'9') {
                width = width * 10 + (*fmt++ - u'0');
            }
        }
        if (width >= 0) {
            specLen += (size_t)std::snprintf(spec + specLen, sizeof(spec) - specLen, "%d", width);
        }
        int precision = -1;
        if (*fmt == u'.') {
            fmt++;
            precision = 0;
            if (*fmt == u'*') {
                precision = va_arg(args, int);
                fmt++;
            } else {
                while (*fmt >= u'0' && *fmt <= u'9') {
                    precision = precision * 10 + (*fmt++ - u'0');
                }
            }
            if (precision >= 0) {
                specLen += (size_t)std::snprintf(spec + specLen, sizeof(spec) - specLen, ".%d", precision);
            }
        }
        int longs = 0;
        int shorts = 0;
        bool size = false;
        bool longDouble = false;
        for (;; fmt++) {
            if (*fmt == u'l') {
                longs++;
            } else if (*fmt == u'h') {
                shorts++;
            } else if (*fmt == u'z' || *fmt == u't' || *fmt == u'j') {
                size = true;
            } else if (*fmt == u'L') {
                longDouble = true;
            } else {
                break;
            }
        }

        char text[512];
        int textLen = 0;
        const char16_t conv = *fmt;
        if (conv == 0) {
            // A specifier cut off by the end of the format: written as it is
            for (; start != fmt; start++) {
                out.put(*start);
            }
            break;
        }
        fmt++;
        switch (conv) {
        case u'd':
        case u'i':
        case u'u':
        case u'o':
        case u'x':
        case u'X': {
            const bool isSigned = conv == u'd' || conv == u'i';
            const char c = (char)conv;
            if (size) {
                spec[specLen] = 'z';
                spec[specLen + 1] = isSigned ? 'd' : c;
                spec[specLen + 2] = 0;
                textLen = std::snprintf(text, sizeof(text), spec, va_arg(args, size_t));
            } else if (longs >= 2) {
                spec[specLen] = 'l';
                spec[specLen + 1] = 'l';
                spec[specLen + 2] = c;
                spec[specLen + 3] = 0;
                textLen = isSigned ? std::snprintf(text, sizeof(text), spec, va_arg(args, long long))
                                   : std::snprintf(text, sizeof(text), spec, va_arg(args, unsigned long long));
            } else if (longs == 1) {
                spec[specLen] = 'l';
                spec[specLen + 1] = c;
                spec[specLen + 2] = 0;
                textLen = isSigned ? std::snprintf(text, sizeof(text), spec, va_arg(args, long))
                                   : std::snprintf(text, sizeof(text), spec, va_arg(args, unsigned long));
            } else {
                spec[specLen] = c;
                spec[specLen + 1] = 0;
                int value = va_arg(args, int);
                if (shorts == 1) {
                    value = isSigned ? (int)(short)value : (int)(unsigned short)value;
                } else if (shorts >= 2) {
                    value = isSigned ? (int)(signed char)value : (int)(unsigned char)value;
                }
                textLen = std::snprintf(text, sizeof(text), spec, value);
            }
            break;
        }
        case u'f':
        case u'F':
        case u'e':
        case u'E':
        case u'g':
        case u'G':
        case u'a':
        case u'A':
            if (longDouble) {
                spec[specLen] = 'L';
                spec[specLen + 1] = (char)conv;
                spec[specLen + 2] = 0;
                textLen = std::snprintf(text, sizeof(text), spec, va_arg(args, long double));
            } else {
                spec[specLen] = (char)conv;
                spec[specLen + 1] = 0;
                textLen = std::snprintf(text, sizeof(text), spec, va_arg(args, double));
            }
            break;
        case u'p':
            spec[specLen] = 'p';
            spec[specLen + 1] = 0;
            textLen = std::snprintf(text, sizeof(text), spec, va_arg(args, void*));
            break;
        case u'c':
        case u's': {
            // Copied as UTF-16 code units: a char (string) widened byte by byte, as MSL does
            const char16_t* wide = nullptr;
            const char* narrow = nullptr;
            char16_t single[1];
            size_t len;
            if (conv == u'c') {
                single[0] = (char16_t)(longs > 0 ? va_arg(args, int) : (unsigned char)va_arg(args, int));
                wide = single;
                len = 1;
            } else if (longs > 0) {
                wide = va_arg(args, const char16_t*);
                if (wide == nullptr) {
                    wide = u"(null)";
                }
                for (len = 0; (precision < 0 || len < (size_t)precision) && wide[len] != 0; len++) {
                }
            } else {
                narrow = va_arg(args, const char*);
                if (narrow == nullptr) {
                    narrow = "(null)";
                }
                for (len = 0; (precision < 0 || len < (size_t)precision) && narrow[len] != 0; len++) {
                }
            }
            const size_t padding = width > 0 && (size_t)width > len ? (size_t)width - len : 0;
            if (!left) {
                out.pad(padding, u' ');
            }
            for (size_t i = 0; i < len; i++) {
                out.put(wide != nullptr ? wide[i] : (char16_t)(unsigned char)narrow[i]);
            }
            if (left) {
                out.pad(padding, u' ');
            }
            continue;
        }
        case u'n':
            *va_arg(args, int*) = (int)out.len;
            continue;
        default:
            // Unknown: written as it is
            for (; start != fmt; start++) {
                out.put(*start);
            }
            continue;
        }

        if (textLen < 0) {
            return -1;
        }
        for (int i = 0; i < textLen && i < (int)sizeof(text) - 1; i++) {
            out.put((char16_t)(unsigned char)text[i]);
        }
    }

    if (n == 0) {
        return -1;
    }
    buf[out.len < n ? out.len : n - 1] = 0;
    // As swprintf: a negative result when the output did not fit
    return out.len < n ? (int)out.len : -1;
}

} // namespace port_wchar16
#endif

inline int vswprintf(char16_t* buf, size_t n, const char16_t* fmt, va_list args) {
#if defined(_MSC_VER)
    int r = __stdio_common_vswprintf(PORT_WPRINTF_OPTIONS, reinterpret_cast<wchar_t*>(buf), n,
                                     reinterpret_cast<const wchar_t*>(fmt), nullptr, args);
    return r < 0 ? -1 : r;
#else
    return port_wchar16::vformat(buf, n, fmt, args);
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
    // Formatted as UTF-16, written as UTF-8
    char16_t text[1024];
    port_wchar16::vformat(text, sizeof(text) / sizeof(text[0]), fmt, args);
    char utf8[sizeof(text) / sizeof(text[0]) * 3];
    int r = (int)wcstombs(utf8, text, sizeof(utf8));
    std::fputs(utf8, stdout);
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
