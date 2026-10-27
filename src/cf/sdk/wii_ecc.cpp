// ECDSA over sect233r1 (wii_ecc.h). The field is GF(2^233) with x^233 + x^74 + 1, elements in 30
// big-endian bytes; the curve is y^2 + xy = x^3 + x^2 + b. Field and point arithmetic follow segher's
// ec.c; the scalars (mod the group order n) are plain little-endian 32-bit limbs.
#include "wii_ecc.h"

#include <cstring>
#include <random>

namespace cf {
namespace sdk {
namespace ecc {

namespace {

using Elt = std::array<std::uint8_t, 30>;

// The group order n and the base point G
const Elt kOrder = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x13, 0xe9, 0x74, 0xe7, 0x2f, 0x8a, 0x69, 0x22, 0x03, 0x1d, 0x26, 0x03, 0xcf, 0xe0, 0xd7};
const PublicKey kBase = {0x00, 0xfa, 0xc9, 0xdf, 0xcb, 0xac, 0x83, 0x13, 0xbb, 0x21, 0x39, 0xf1, 0xbb, 0x75, 0x5f,
                         0xef, 0x65, 0xbc, 0x39, 0x1f, 0x8b, 0x36, 0xf8, 0xf8, 0xeb, 0x73, 0x71, 0xfd, 0x55, 0x8b,
                         0x01, 0x00, 0x6a, 0x08, 0xa4, 0x19, 0x03, 0x35, 0x06, 0x78, 0xe5, 0x85, 0x28, 0xbe, 0xbf,
                         0x8a, 0x0b, 0xef, 0xf8, 0x67, 0xa7, 0xca, 0x36, 0x71, 0x6f, 0x7e, 0x01, 0xf8, 0x10, 0x52};

// ---------------------------------------------------------------------------------------------
// GF(2^233)
// ---------------------------------------------------------------------------------------------
bool IsZero(const Elt& a) {
    for (std::uint8_t b : a) {
        if (b != 0) {
            return false;
        }
    }
    return true;
}

Elt Add(const Elt& a, const Elt& b) {
    Elt d;
    for (int i = 0; i < 30; i++) {
        d[i] = a[i] ^ b[i];
    }
    return d;
}

// a * x
Elt MulX(const Elt& a) {
    Elt d;
    const std::uint8_t carry = a[0] & 1;
    std::uint8_t x = 0;
    for (int i = 0; i < 29; i++) {
        const std::uint8_t y = a[i + 1];
        d[i] = x ^ (y >> 7);
        x = static_cast<std::uint8_t>(y << 1);
    }
    d[29] = x ^ carry;
    d[20] ^= static_cast<std::uint8_t>(carry << 2);
    return d;
}

Elt Mul(const Elt& a, const Elt& b) {
    Elt d{};
    int i = 0;
    std::uint8_t mask = 1;
    for (int n = 0; n < 233; n++) {
        d = MulX(d);
        if (a[i] & mask) {
            d = Add(d, b);
        }
        mask >>= 1;
        if (mask == 0) {
            mask = 0x80;
            i++;
        }
    }
    return d;
}

Elt Square(const Elt& a) {
    static const std::uint8_t kSpread[16] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15,
                                             0x40, 0x41, 0x44, 0x45, 0x50, 0x51, 0x54, 0x55};
    std::uint8_t wide[60];
    for (int i = 0; i < 30; i++) {
        wide[2 * i] = kSpread[a[i] >> 4];
        wide[2 * i + 1] = kSpread[a[i] & 15];
    }
    for (int i = 0; i < 30; i++) {
        const std::uint8_t x = wide[i];
        wide[i + 19] ^= x >> 7;
        wide[i + 20] ^= static_cast<std::uint8_t>(x << 1);
        wide[i + 29] ^= x >> 1;
        wide[i + 30] ^= static_cast<std::uint8_t>(x << 7);
    }
    const std::uint8_t x = wide[30] & 0xfe;
    wide[49] ^= x >> 7;
    wide[50] ^= static_cast<std::uint8_t>(x << 1);
    wide[59] ^= x >> 1;
    wide[30] &= 1;
    Elt d;
    std::memcpy(d.data(), wide + 30, 30);
    return d;
}

// a^(2^j) * b
Elt ItohTsujii(const Elt& a, const Elt& b, int j) {
    Elt t = a;
    for (; j != 0; j--) {
        t = Square(t);
    }
    return Mul(t, b);
}

Elt Inverse(const Elt& a) {
    Elt t = ItohTsujii(a, a, 1);
    Elt s = ItohTsujii(t, a, 1);
    t = ItohTsujii(s, s, 3);
    s = ItohTsujii(t, a, 1);
    t = ItohTsujii(s, s, 7);
    s = ItohTsujii(t, t, 14);
    t = ItohTsujii(s, a, 1);
    s = ItohTsujii(t, t, 29);
    t = ItohTsujii(s, s, 58);
    s = ItohTsujii(t, t, 116);
    return Square(s);
}

// ---------------------------------------------------------------------------------------------
// Points (the zero point, the identity, is all zero)
// ---------------------------------------------------------------------------------------------
Elt X(const PublicKey& p) {
    Elt x;
    std::memcpy(x.data(), p.data(), 30);
    return x;
}

Elt Y(const PublicKey& p) {
    Elt y;
    std::memcpy(y.data(), p.data() + 30, 30);
    return y;
}

PublicKey MakePoint(const Elt& x, const Elt& y) {
    PublicKey p;
    std::memcpy(p.data(), x.data(), 30);
    std::memcpy(p.data() + 30, y.data(), 30);
    return p;
}

bool IsZero(const PublicKey& p) {
    for (std::uint8_t b : p) {
        if (b != 0) {
            return false;
        }
    }
    return true;
}

PublicKey Double(const PublicKey& p) {
    const Elt px = X(p);
    const Elt py = Y(p);
    if (IsZero(px)) {
        return PublicKey{};
    }
    Elt t = Inverse(px);
    Elt s = Add(Mul(py, t), px);
    t = Square(px);
    Elt rx = Add(Square(s), s);
    rx[29] ^= 1;
    const Elt ry = Add(Add(Mul(s, rx), rx), t);
    return MakePoint(rx, ry);
}

PublicKey AddPoints(const PublicKey& p, const PublicKey& q) {
    if (IsZero(p)) {
        return q;
    }
    if (IsZero(q)) {
        return p;
    }
    const Elt px = X(p), py = Y(p), qx = X(q), qy = Y(q);
    Elt u = Add(px, qx);
    if (IsZero(u)) {
        u = Add(py, qy);
        return IsZero(u) ? Double(p) : PublicKey{};
    }
    Elt t = Inverse(u);
    u = Add(py, qy);
    Elt s = Mul(t, u);
    t = Add(Add(Square(s), s), qx);
    t[29] ^= 1;
    u = Mul(s, t);
    s = Add(u, py);
    const Elt rx = Add(t, px);
    const Elt ry = Add(s, rx);
    return MakePoint(rx, ry);
}

PublicKey Multiply(const Elt& k, const PublicKey& p) {
    PublicKey d{};
    for (int i = 0; i < 30; i++) {
        for (std::uint8_t mask = 0x80; mask != 0; mask >>= 1) {
            d = Double(d);
            if (k[i] & mask) {
                d = AddPoints(d, p);
            }
        }
    }
    return d;
}

// ---------------------------------------------------------------------------------------------
// Scalars mod n: 8 little-endian 32-bit limbs (n is 233 bits)
// ---------------------------------------------------------------------------------------------
using Num = std::array<std::uint32_t, 8>;

Num ToNum(const std::uint8_t* bytes, int size) {
    Num a{};
    for (int i = 0; i < size; i++) {
        const int bit = (size - 1 - i) * 8;
        a[bit / 32] |= static_cast<std::uint32_t>(bytes[i]) << (bit % 32);
    }
    return a;
}

Elt ToElt(const Num& a) {
    Elt e;
    for (int i = 0; i < 30; i++) {
        const int bit = (29 - i) * 8;
        e[i] = static_cast<std::uint8_t>(a[bit / 32] >> (bit % 32));
    }
    return e;
}

const Num& Order() {
    static const Num n = ToNum(kOrder.data(), 30);
    return n;
}

int Compare(const Num& a, const Num& b) {
    for (int i = 7; i >= 0; i--) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

bool IsZero(const Num& a) {
    for (std::uint32_t w : a) {
        if (w != 0) {
            return false;
        }
    }
    return true;
}

Num Sub(const Num& a, const Num& b) {
    Num d;
    std::uint64_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        const std::uint64_t x = static_cast<std::uint64_t>(a[i]) - b[i] - borrow;
        d[i] = static_cast<std::uint32_t>(x);
        borrow = (x >> 32) & 1;
    }
    return d;
}

// (a + b) mod n, for a, b < n (no overflow: n < 2^234)
Num AddMod(const Num& a, const Num& b) {
    Num d;
    std::uint64_t carry = 0;
    for (int i = 0; i < 8; i++) {
        const std::uint64_t x = static_cast<std::uint64_t>(a[i]) + b[i] + carry;
        d[i] = static_cast<std::uint32_t>(x);
        carry = x >> 32;
    }
    return Compare(d, Order()) >= 0 ? Sub(d, Order()) : d;
}

// a mod n, for a < 2^256
Num Reduce(Num a) {
    // n * 2^k for the largest k below 2^256, subtracted down to k = 0
    for (int shift = 23; shift >= 0; shift--) {
        Num m{};
        for (int bit = 0; bit < 256 - shift; bit++) {
            if ((Order()[bit / 32] >> (bit % 32)) & 1) {
                const int to = bit + shift;
                m[to / 32] |= 1u << (to % 32);
            }
        }
        if (Compare(a, m) >= 0) {
            a = Sub(a, m);
        }
    }
    return a;
}

// a * b mod n (double and add)
Num MulMod(const Num& a, const Num& b) {
    Num d{};
    for (int bit = 255; bit >= 0; bit--) {
        d = AddMod(d, d);
        if ((b[bit / 32] >> (bit % 32)) & 1) {
            d = AddMod(d, a);
        }
    }
    return d;
}

// a^-1 mod n: a^(n - 2), n being prime
Num InvMod(const Num& a) {
    Num e = Order();
    e[0] -= 2; // n is odd and its low limb is above 2
    Num d{};
    d[0] = 1;
    for (int bit = 255; bit >= 0; bit--) {
        d = MulMod(d, d);
        if ((e[bit / 32] >> (bit % 32)) & 1) {
            d = MulMod(d, a);
        }
    }
    return d;
}

// A random scalar in [1, n - 1]
Num RandomScalar() {
    static std::random_device random;
    for (;;) {
        std::uint8_t bytes[30];
        for (auto& b : bytes) {
            b = static_cast<std::uint8_t>(random());
        }
        bytes[0] &= 0x01; // n is 233 bits
        const Num k = ToNum(bytes, 30);
        if (!IsZero(k) && Compare(k, Order()) < 0) {
            return k;
        }
    }
}

} // namespace

PublicKey MakePublicKey(const PrivateKey& privateKey) {
    return Multiply(privateKey, kBase);
}

PrivateKey MakePrivateKey() {
    return ToElt(RandomScalar());
}

Signature Sign(const PrivateKey& privateKey, const std::uint8_t digest[20]) {
    const Num e = ToNum(digest, 20);
    const Num d = ToNum(privateKey.data(), 30);
    for (;;) {
        const Num k = RandomScalar();
        const PublicKey point = Multiply(ToElt(k), kBase);
        const Num r = Reduce(ToNum(point.data(), 30));
        if (IsZero(r)) {
            continue;
        }
        const Num s = MulMod(InvMod(k), AddMod(Reduce(e), MulMod(r, d)));
        if (IsZero(s)) {
            continue;
        }
        Signature signature;
        const Elt re = ToElt(r), se = ToElt(s);
        std::memcpy(signature.data(), re.data(), 30);
        std::memcpy(signature.data() + 30, se.data(), 30);
        return signature;
    }
}

bool Verify(const PublicKey& publicKey, const Signature& signature, const std::uint8_t digest[20]) {
    const Num r = ToNum(signature.data(), 30);
    const Num s = ToNum(signature.data() + 30, 30);
    if (IsZero(r) || IsZero(s) || Compare(r, Order()) >= 0 || Compare(s, Order()) >= 0) {
        return false;
    }
    const Num w = InvMod(s);
    const Num u1 = MulMod(Reduce(ToNum(digest, 20)), w);
    const Num u2 = MulMod(r, w);
    const PublicKey point = AddPoints(Multiply(ToElt(u1), kBase), Multiply(ToElt(u2), publicKey));
    return Compare(Reduce(ToNum(point.data(), 30)), r) == 0;
}

} // namespace ecc
} // namespace sdk
} // namespace cf
