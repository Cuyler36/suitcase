// MSL's non-standard string functions for hosts whose C library lacks them (see include/extras.h).
#include <extras.h>

#include <ctype.h>
#include <wctype.h>

int __msl_wcsnicmp(const unsigned short* a, const unsigned short* b, size_t n) {
    wint_t ca = 0, cb = 0;
    while (n-- > 0) {
        ca = towlower((wint_t)*a++);
        cb = towlower((wint_t)*b++);
        if (ca != cb || ca == 0) {
            break;
        }
    }
    return (int)ca - (int)cb;
}

#if !defined(_MSC_VER)
#include <ctype.h>

int stricmp(const char* a, const char* b) {
    int ca, cb;
    do {
        ca = tolower((unsigned char)*a++);
        cb = tolower((unsigned char)*b++);
    } while (ca == cb && ca != '\0');
    return ca - cb;
}

int strnicmp(const char* a, const char* b, size_t n) {
    int ca = 0, cb = 0;
    while (n-- > 0) {
        ca = tolower((unsigned char)*a++);
        cb = tolower((unsigned char)*b++);
        if (ca != cb || ca == '\0') {
            break;
        }
    }
    return ca - cb;
}
#endif
