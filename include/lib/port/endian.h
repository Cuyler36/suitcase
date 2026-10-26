// Big-endian data on a little-endian host (the PC port). Adapted from Dusklight's
// include/helpers/endian.h (CC0).
//
// Everything on disc, and every resource read in place, is big-endian. On PC:
// - BE(T) declares a struct field that holds a big-endian T: reads and writes convert, so code that
//   uses the field is unchanged (including comparisons with multi-character constants like 'INF1').
// - RES_U16/RES_S16/RES_U32/RES_S32/RES_F32 convert one value read from big-endian memory; they also
//   work in C, where BE(T) cannot.
// - be_swap() converts a value (or array) in place.
// On the Wii all of these are no-ops: BE(T) is T and RES_*(x) is x.
#ifndef PORT_ENDIAN_H
#define PORT_ENDIAN_H

#if TARGET_PC

// revolution/types.h works in C and C++ (the SDK's C units do not see include/types.h)
#include <revolution/types.h>
#ifdef __cplusplus
#include <revolution/mtx.h>
#endif

#if defined(_MSC_VER)
#include <stdlib.h>
#define PORT_BSWAP16(x) _byteswap_ushort(x)
#define PORT_BSWAP32(x) _byteswap_ulong(x)
#define PORT_BSWAP64(x) _byteswap_uint64(x)
#else
#define PORT_BSWAP16(x) __builtin_bswap16(x)
#define PORT_BSWAP32(x) __builtin_bswap32(x)
#define PORT_BSWAP64(x) __builtin_bswap64(x)
#endif

#ifdef __cplusplus

#include <bit>
#include <type_traits>

extern "C++" {

// MSVC's _byteswap_* are not constexpr; these are used in constant evaluation
constexpr u16 be16_manual(u16 val) {
    return (u16)((val >> 8) | (val << 8));
}

constexpr u32 be32_manual(u32 val) {
    return (val >> 24) | ((val << 8) & 0x00FF0000) | ((val >> 8) & 0x0000FF00) | (val << 24);
}

constexpr u64 be64_manual(u64 val) {
    return (val >> 56) | ((val << 40) & 0x00FF000000000000) | ((val << 24) & 0x0000FF0000000000) |
           ((val << 8) & 0x000000FF00000000) | ((val >> 8) & 0x00000000FF000000) |
           ((val >> 24) & 0x0000000000FF0000) | ((val >> 40) & 0x000000000000FF00) | (val << 56);
}

constexpr u16 be16(u16 val) {
    if (std::is_constant_evaluated()) {
        return be16_manual(val);
    }
    return PORT_BSWAP16(val);
}

constexpr u32 be32(u32 val) {
    if (std::is_constant_evaluated()) {
        return be32_manual(val);
    }
    return PORT_BSWAP32(val);
}

constexpr u64 be64(u64 val) {
    if (std::is_constant_evaluated()) {
        return be64_manual(val);
    }
    return PORT_BSWAP64(val);
}

constexpr u16 RES_U16(u16 v) {
    return be16(v);
}
constexpr s16 RES_S16(s16 v) {
    return (s16)be16((u16)v);
}
constexpr u32 RES_U32(u32 v) {
    return be32(v);
}
constexpr s32 RES_S32(s32 v) {
    return (s32)be32((u32)v);
}
constexpr u64 RES_U64(u64 v) {
    return be64(v);
}
constexpr s64 RES_S64(s64 v) {
    return (s64)be64((u64)v);
}
constexpr f32 RES_F32(f32 v) {
    return std::bit_cast<f32>(be32(std::bit_cast<u32>(v)));
}

// A big-endian T stored in place
template <class T>
struct BE {
    T inner;

    constexpr BE() noexcept = default;
    constexpr BE(const T& from) noexcept : inner(swap(from)) {}

    constexpr T operator--(int) {
        T orig = *this;
        *this = orig - 1;
        return orig;
    }

    constexpr T operator++(int) {
        T orig = *this;
        *this = orig + 1;
        return orig;
    }

    constexpr operator T() const noexcept {
        return swap(inner);
    }

    [[nodiscard]] constexpr T host() const noexcept {
        return swap(inner);
    }

    // Any 1/2/4/8-byte integer or enum, f32 or f64; aggregates (Vec, Mtx, ...) are specialized below
    [[nodiscard]] static constexpr T swap(T val) noexcept {
        if constexpr (sizeof(T) == 1) {
            return val;
        } else if constexpr (sizeof(T) == 2) {
            return std::bit_cast<T>(be16(std::bit_cast<u16>(val)));
        } else if constexpr (sizeof(T) == 4) {
            return std::bit_cast<T>(be32(std::bit_cast<u32>(val)));
        } else {
            static_assert(sizeof(T) == 8, "BE<T>: no byte swap for this type");
            return std::bit_cast<T>(be64(std::bit_cast<u64>(val)));
        }
    }
};

#define PORT_BE_ASSIGN_OP(op)                                                                      \
    template <typename TA, typename TB>                                                            \
    constexpr BE<TA>& operator op(BE<TA>& a, TB b) {                                               \
        TA copy = a;                                                                               \
        copy op b;                                                                                 \
        a = copy;                                                                                  \
        return a;                                                                                  \
    }

PORT_BE_ASSIGN_OP(&=)
PORT_BE_ASSIGN_OP(|=)
PORT_BE_ASSIGN_OP(+=)
PORT_BE_ASSIGN_OP(-=)
PORT_BE_ASSIGN_OP(*=)
PORT_BE_ASSIGN_OP(/=)
PORT_BE_ASSIGN_OP(^=)

#undef PORT_BE_ASSIGN_OP


template <>
constexpr S16Vec BE<S16Vec>::swap(S16Vec val) noexcept {
    return {RES_S16(val.x), RES_S16(val.y), RES_S16(val.z)};
}

template <>
struct BE<Vec> {
    BE<f32> x;
    BE<f32> y;
    BE<f32> z;

    constexpr BE() noexcept = default;
    constexpr BE(f32 x_, f32 y_, f32 z_) noexcept : x(x_), y(y_), z(z_) {}
    constexpr BE(const Vec& from) noexcept : x(from.x), y(from.y), z(from.z) {}

    constexpr operator Vec() const noexcept {
        return {x, y, z};
    }

    static constexpr Vec swap(Vec val) noexcept {
        return {RES_F32(val.x), RES_F32(val.y), RES_F32(val.z)};
    }
};

template <>
struct BE<Mtx> {
    BE<f32> contents[3][4];

    constexpr auto& operator[](int row) noexcept {
        return contents[row];
    }
    constexpr auto& operator[](int row) const noexcept {
        return contents[row];
    }

    constexpr void to_host(Mtx& mtx) const noexcept {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                mtx[i][j] = contents[i][j];
            }
        }
    }
};

template <>
struct BE<Mtx44> {
    BE<f32> contents[4][4];

    constexpr auto& operator[](int row) noexcept {
        return contents[row];
    }
    constexpr auto& operator[](int row) const noexcept {
        return contents[row];
    }
};

// Converts in place: after be_swap the value is in host order (or back again)
template <typename T>
constexpr void be_swap(T& val) noexcept {
    val = BE<T>::swap(val);
}

template <typename T, u32 N>
constexpr void be_swap(T (&val)[N]) noexcept {
    for (u32 i = 0; i < N; i++) {
        be_swap(val[i]);
    }
}

template <typename T>
constexpr void be_swap(T array[], u32 size) noexcept {
    for (u32 i = 0; i < size; i++) {
        be_swap(array[i]);
    }
}

} // extern "C++"

#define BE(T) BE<T>
#define BE_HOST(x) ((x).host())

#else // C

static inline u16 RES_U16(u16 v) {
    return PORT_BSWAP16(v);
}
static inline s16 RES_S16(s16 v) {
    return (s16)PORT_BSWAP16((u16)v);
}
static inline u32 RES_U32(u32 v) {
    return PORT_BSWAP32(v);
}
static inline s32 RES_S32(s32 v) {
    return (s32)PORT_BSWAP32((u32)v);
}
static inline f32 RES_F32(f32 v) {
    union {
        f32 f;
        u32 u;
    } c;
    c.f = v;
    c.u = PORT_BSWAP32(c.u);
    return c.f;
}

#endif // __cplusplus

#else // !TARGET_PC: the Wii is big-endian

#define BE(T) T
#define BE_HOST(x) (x)
#define RES_U16(x) (x)
#define RES_S16(x) (x)
#define RES_U32(x) (x)
#define RES_S32(x) (x)
#define RES_F32(x) (x)

#endif // TARGET_PC

#endif // PORT_ENDIAN_H
