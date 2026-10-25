#ifndef GLOBAL_HEADERS_DECOMP_UTILS_H
#define GLOBAL_HEADERS_DECOMP_UTILS_H

#if __cplusplus < 201103L
#define noexcept throw()
#endif

#if TARGET_PC
// Same as include/types.h
#ifndef DECL_WEAK
#if defined(__GNUC__) || defined(__clang__)
#define DECL_WEAK __attribute__((weak))
#else
#define DECL_WEAK
#endif
#endif
#else
#define DECL_WEAK __declspec(weak)
#endif

#define __CONCAT(x, y) x##y
#define CONCAT(x, y) __CONCAT(x, y)

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

#define ROUNDUP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define PTR_ROUNDUP(x, a) ((void*)ROUNDUP((unsigned long)x, a))

#define ROUNDDOWN(x, a) (((x)) & ~((a) - 1))
#define PTR_ROUNDDOWN(x, a) ((void*)ROUNDDOWN((unsigned long)x, a))

#define MEM_CLEAR(x) __memclr((x), sizeof(*(x)))

#ifdef __MWERKS__
#define ADDRESS(addr) : (addr)
#else
#define ADDRESS(addr)
#endif

#ifdef __cplusplus
#ifdef __MWERKS__
#define override
#endif
#endif

/* Macros for Matching */

#if TARGET_PC
// Matching hacks compile out on PC (MSVC also rejects GNU named variadic macros)
#define NO_INLINE
#define DECOMP_FORCE_ACTIVE(module, ...)
#define DECOMP_FORCE_STRING(module, str)
#define DECOMP_FORCE_LITERAL(module, ...)
#define DECOMP_FORCE_DTOR(module, cls)
#elif !defined(NON_MATCHING)

#define NO_INLINE __attribute__((never_inline))

#define DECOMP_FORCE_ACTIVE(module, args...)                                                                                                         \
    void fake_function(int a, ...);                                                                                                                  \
    void CONCAT(FORCEACTIVE##module, __LINE__)();                                                                                                    \
    void CONCAT(FORCEACTIVE##module, __LINE__)() {                                                                                                   \
        fake_function(0, args);                                                                                                                      \
    }


/*
 * Strings of functions the linker stripped from the retail build still sit in the TU's string pool.
 * A global char array lands in .data at its position in the source, 4-byte aligned like the pooled literal.
 */
#define DECOMP_FORCE_STRING(module, str) char CONCAT(CONCAT(FORCESTRING, module), __LINE__)[] = str;

#define DECOMP_FORCE_LITERAL(module, ...)                                                                                                            \
    void CONCAT(FORCELITERAL##module, __LINE__)();                                                                                                   \
    void CONCAT(FORCELITERAL##module, __LINE__)() {                                                                                                  \
        (__VA_ARGS__);                                                                                                                               \
    }

#define DECOMP_FORCE_DTOR(module, cls)                                                                                                               \
    void CONCAT(FORCEDTOR##module##cls, __LINE__)();                                                                                                 \
    void CONCAT(FORCEDTOR##module##cls, __LINE__)() {                                                                                                \
        cls dummy;                                                                                                                                   \
        dummy.~cls();                                                                                                                                \
    }

#else
// #define NO_INLINE
#define NO_INLINE __attribute__((never_inline)) /* for now */
#define DECOMP_FORCE_ACTIVE(module, args...)
#define DECOMP_FORCE_LITERAL(module, ...)
#define DECOMP_FORCE_DTOR(module, cls)
#endif  // NON_MATCHING

#endif  // GLOBAL_HEADERS_DECOMP_UTILS_H
