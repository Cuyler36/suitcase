#ifndef NW4R_UT_COLOR_H
#define NW4R_UT_COLOR_H
#include <nw4r/types_nw4r.h>

#include <revolution/gx.h>

namespace nw4r {
namespace ut {

struct Color : public GXColor {
public:
    Color() {
        *this = WHITE;
    }
    Color(ulong color) {
        *this = color;
    }
    Color(int red, int green, int blue, int alpha) {
        Set(red, green, blue, alpha);
    }
    Color(const GXColor& rColor) {
        *this = rColor;
    }

    ~Color() {}

    void Set(int red, int green, int blue, int alpha) {
        r = red;
        g = green;
        b = blue;
        a = alpha;
    }

#if TARGET_PC
    // A u32 color is 0xRRGGBBAA, which is the byte order only on a big-endian host
    Color& operator=(ulong color) {
        Set(static_cast<u8>(color >> 24), static_cast<u8>(color >> 16),
            static_cast<u8>(color >> 8), static_cast<u8>(color));
        return *this;
    }
    Color& operator=(const GXColor& rColor) {
        Set(rColor.r, rColor.g, rColor.b, rColor.a);
        return *this;
    }
#else
    Color& operator=(ulong color) {
        ToU32ref() = color;
        return *this;
    }
    Color& operator=(const GXColor& rColor) {
        *this = *reinterpret_cast<const ulong*>(&rColor);
        return *this;
    }
#endif

    Color operator|(ulong color) const {
        return Color(ToU32() | color);
    }
    Color operator&(ulong color) const {
        return Color(ToU32() & color);
    }

    ulong& ToU32ref() {
        return *reinterpret_cast<ulong*>(this);
    }
    const ulong& ToU32ref() const {
        return *reinterpret_cast<const ulong*>(this);
    }

#if TARGET_PC
    ulong ToU32() const {
        return static_cast<ulong>(r) << 24 | static_cast<ulong>(g) << 16 |
               static_cast<ulong>(b) << 8 | static_cast<ulong>(a);
    }

    operator ulong() const {
        return ToU32();
    }
#else
    ulong ToU32() const {
        return ToU32ref();
    }

    operator ulong() const {
        return ToU32ref();
    }
#endif

    // clang-format off
    static const ulong RED   = 0xFF0000FF;
    static const ulong GREEN = 0x00FF00FF;
    static const ulong BLUE  = 0x0000FFFF;

    static const ulong CYAN    = 0x00FFFFFF;
    static const ulong MAGENTA = 0xFF00FFFF;
    static const ulong YELLOW  = 0xFFFF00FF;

    static const ulong BLACK = 0x000000FF;
    static const ulong GRAY  = 0x808080FF;
    static const ulong WHITE = 0xFFFFFFFF;
    // clang-format on
} ALIGN(4);

} // namespace ut
} // namespace nw4r

#endif
