// The RevoEX NET library's hashing and AES (revolution/net/NETDigest.h, NETAES.h) on the host: SHA-1,
// HMAC and AES-CBC, as the SDK's (lib/RevoEX/src/net: FIPS-197 AES, standard HMAC), so their output is
// a Wii's byte for byte. The game uses them to sign and encrypt its WiiConnect24 letters
// (src/dol/game/d_net_wc24.cpp).
//
// The contexts live in memory the caller provides and sizes for the Wii: NETHMACContext (the game uses
// the SDK type) and a 0x104-byte buffer for NETAESContext (the game's own size; the SDK header's
// inferred one is 0x108). The host state here fits both and does not use their fields.
#include <revolution/net.h>

#include "net_crypto.h"

#include <cstddef>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------------------------
// SHA-1 (FIPS 180-4)
// ---------------------------------------------------------------------------------------------
struct Sha1 {
    u32 h[5];
    u8 block[64];
    u32 used;    // bytes in block
    u64 length;  // message bytes so far
};

u32 Rotl(u32 x, int n) {
    return (x << n) | (x >> (32 - n));
}

void Sha1Block(Sha1* s, const u8* p) {
    u32 w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (u32(p[i * 4]) << 24) | (u32(p[i * 4 + 1]) << 16) | (u32(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    u32 a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        const u32 t = Rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rotl(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

void Sha1Init(void* context) {
    Sha1* s = static_cast<Sha1*>(context);
    s->h[0] = 0x67452301;
    s->h[1] = 0xEFCDAB89;
    s->h[2] = 0x98BADCFE;
    s->h[3] = 0x10325476;
    s->h[4] = 0xC3D2E1F0;
    s->used = 0;
    s->length = 0;
}

void Sha1Update(void* context, const void* data, u32 size) {
    Sha1* s = static_cast<Sha1*>(context);
    const u8* p = static_cast<const u8*>(data);
    s->length += size;
    while (size > 0) {
        const u32 n = size < 64 - s->used ? size : 64 - s->used;
        std::memcpy(s->block + s->used, p, n);
        s->used += n;
        p += n;
        size -= n;
        if (s->used == 64) {
            Sha1Block(s, s->block);
            s->used = 0;
        }
    }
}

void Sha1Digest(void* context, void* digest) {
    Sha1* s = static_cast<Sha1*>(context);
    const u64 bits = s->length * 8;
    const u8 pad = 0x80;
    Sha1Update(s, &pad, 1);
    const u8 zero = 0;
    while (s->used != 56) {
        Sha1Update(s, &zero, 1);
    }
    u8 len[8];
    for (int i = 0; i < 8; i++) {
        len[i] = static_cast<u8>(bits >> (56 - i * 8));
    }
    Sha1Update(s, len, 8);
    u8* out = static_cast<u8*>(digest);
    for (int i = 0; i < 5; i++) {
        out[i * 4] = static_cast<u8>(s->h[i] >> 24);
        out[i * 4 + 1] = static_cast<u8>(s->h[i] >> 16);
        out[i * 4 + 2] = static_cast<u8>(s->h[i] >> 8);
        out[i * 4 + 3] = static_cast<u8>(s->h[i]);
    }
}

constexpr u32 SHA1_LENGTH = 20;
constexpr u32 SHA1_BLOCK = 64;

// ---------------------------------------------------------------------------------------------
// HMAC (RFC 2104) over SHA-1, the only hash the game uses
// ---------------------------------------------------------------------------------------------
struct Hmac {
    Sha1 inner;
    u8 key[SHA1_BLOCK]; // the key, zero-padded to the block
};
static_assert(sizeof(Hmac) <= sizeof(NETHMACContext), "the HMAC state must fit the caller's NETHMACContext");

// ---------------------------------------------------------------------------------------------
// AES (FIPS-197), CBC
// ---------------------------------------------------------------------------------------------
const u8 kSbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9,
    0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F,
    0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15, 0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07,
    0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3,
    0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58,
    0xCF, 0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3,
    0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC, 0x5F,
    0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73, 0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88,
    0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC,
    0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A,
    0xAE, 0x08, 0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A, 0x70,
    0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF, 0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42,
    0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16,
};

u8 kInvSbox[256];

void InitInvSbox() {
    static bool done = false;
    if (!done) {
        for (int i = 0; i < 256; i++) {
            kInvSbox[kSbox[i]] = static_cast<u8>(i);
        }
        done = true;
    }
}

u8 Xtime(u8 x) {
    return static_cast<u8>((x << 1) ^ ((x & 0x80) ? 0x1B : 0));
}

u8 Mul(u8 a, u8 b) {
    u8 r = 0;
    while (b != 0) {
        if (b & 1) {
            r ^= a;
        }
        a = Xtime(a);
        b >>= 1;
    }
    return r;
}

// The round keys of AES-128/192/256 (up to 15 x 16 bytes), the chaining IV and the round count:
// 0x104 bytes, the game's buffer for the context
struct Aes {
    u8 roundKeys[240];
    u8 iv[16];
    u32 rounds;
};
static_assert(sizeof(Aes) <= 0x104, "the AES state must fit the game's 0x104-byte context buffer");

bool AesExpandKey(Aes* aes, const u8* key, u32 keyLength) {
    if (keyLength != 16 && keyLength != 24 && keyLength != 32) {
        return false;
    }
    const u32 nk = keyLength / 4;
    aes->rounds = nk + 6;
    const u32 words = 4 * (aes->rounds + 1);
    u8* w = aes->roundKeys;
    std::memcpy(w, key, keyLength);
    u8 rcon = 1;
    for (u32 i = nk; i < words; i++) {
        u8 t[4];
        std::memcpy(t, w + (i - 1) * 4, 4);
        if (i % nk == 0) {
            const u8 first = t[0];
            t[0] = static_cast<u8>(kSbox[t[1]] ^ rcon);
            t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];
            t[3] = kSbox[first];
            rcon = Xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (int k = 0; k < 4; k++) {
                t[k] = kSbox[t[k]];
            }
        }
        for (int k = 0; k < 4; k++) {
            w[i * 4 + k] = w[(i - nk) * 4 + k] ^ t[k];
        }
    }
    return true;
}

void AddRoundKey(u8 s[16], const u8* k) {
    for (int i = 0; i < 16; i++) {
        s[i] ^= k[i];
    }
}

// The state is column-major: s[c * 4 + r]
void EncryptBlock(const Aes* aes, u8 s[16]) {
    AddRoundKey(s, aes->roundKeys);
    for (u32 round = 1; round <= aes->rounds; round++) {
        u8 t[16];
        for (int c = 0; c < 4; c++) {
            for (int r = 0; r < 4; r++) {
                t[c * 4 + r] = kSbox[s[((c + r) % 4) * 4 + r]]; // SubBytes + ShiftRows
            }
        }
        if (round != aes->rounds) {
            for (int c = 0; c < 4; c++) {
                u8* col = t + c * 4;
                const u8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                const u8 all = a0 ^ a1 ^ a2 ^ a3;
                col[0] ^= all ^ Xtime(a0 ^ a1);
                col[1] ^= all ^ Xtime(a1 ^ a2);
                col[2] ^= all ^ Xtime(a2 ^ a3);
                col[3] ^= all ^ Xtime(a3 ^ a0);
            }
        }
        std::memcpy(s, t, 16);
        AddRoundKey(s, aes->roundKeys + round * 16);
    }
}

void DecryptBlock(const Aes* aes, u8 s[16]) {
    AddRoundKey(s, aes->roundKeys + aes->rounds * 16);
    for (u32 round = aes->rounds; round >= 1; round--) {
        u8 t[16];
        for (int c = 0; c < 4; c++) {
            for (int r = 0; r < 4; r++) {
                t[((c + r) % 4) * 4 + r] = kInvSbox[s[c * 4 + r]]; // InvShiftRows + InvSubBytes
            }
        }
        AddRoundKey(t, aes->roundKeys + (round - 1) * 16);
        if (round != 1) {
            for (int c = 0; c < 4; c++) {
                u8* col = t + c * 4;
                const u8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = Mul(a0, 14) ^ Mul(a1, 11) ^ Mul(a2, 13) ^ Mul(a3, 9);
                col[1] = Mul(a0, 9) ^ Mul(a1, 14) ^ Mul(a2, 11) ^ Mul(a3, 13);
                col[2] = Mul(a0, 13) ^ Mul(a1, 9) ^ Mul(a2, 14) ^ Mul(a3, 11);
                col[3] = Mul(a0, 11) ^ Mul(a1, 13) ^ Mul(a2, 9) ^ Mul(a3, 14);
            }
        }
        std::memcpy(s, t, 16);
    }
}

const NETHashInterface kSha1Interface = {
    SHA1_LENGTH,
    SHA1_BLOCK,
    sizeof(Sha1),
    nullptr,
    reinterpret_cast<NETHashInitFunc>(Sha1Init),
    reinterpret_cast<NETHashUpdateFunc>(Sha1Update),
    reinterpret_cast<NETHashGetDigestFunc>(Sha1Digest),
};

} // namespace

extern "C" {

const NETHashInterface* NETGetSHA1Interface(void) {
    return &kSha1Interface;
}

void NETHMACInit(NETHMACContext* context, const NETHashInterface* hashInterface, void* key, u32 keyLength) {
    (void)hashInterface; // SHA-1, the only one the game uses
    Hmac* h = reinterpret_cast<Hmac*>(context);
    std::memset(h->key, 0, sizeof(h->key));
    if (keyLength > SHA1_BLOCK) {
        Sha1 s;
        Sha1Init(&s);
        Sha1Update(&s, key, keyLength);
        Sha1Digest(&s, h->key);
    } else {
        std::memcpy(h->key, key, keyLength);
    }
    u8 pad[SHA1_BLOCK];
    for (u32 i = 0; i < SHA1_BLOCK; i++) {
        pad[i] = h->key[i] ^ 0x36;
    }
    Sha1Init(&h->inner);
    Sha1Update(&h->inner, pad, SHA1_BLOCK);
}

void NETHMACUpdate(NETHMACContext* context, void* message, u32 length) {
    Sha1Update(&reinterpret_cast<Hmac*>(context)->inner, message, length);
}

void NETHMACGetDigest(NETHMACContext* context, void* digest) {
    Hmac* h = reinterpret_cast<Hmac*>(context);
    u8 inner[SHA1_LENGTH];
    Sha1Digest(&h->inner, inner);
    u8 pad[SHA1_BLOCK];
    for (u32 i = 0; i < SHA1_BLOCK; i++) {
        pad[i] = h->key[i] ^ 0x5C;
    }
    Sha1 outer;
    Sha1Init(&outer);
    Sha1Update(&outer, pad, SHA1_BLOCK);
    Sha1Update(&outer, inner, SHA1_LENGTH);
    Sha1Digest(&outer, digest);
}

BOOL NETAESCreate(NETAESContext* context, const void* key, u32 keyLength, const void* iv) {
    InitInvSbox();
    Aes* aes = reinterpret_cast<Aes*>(context);
    if (!AesExpandKey(aes, static_cast<const u8*>(key), keyLength)) {
        return FALSE;
    }
    std::memcpy(aes->iv, iv, sizeof(aes->iv));
    return TRUE;
}

void NETAESDelete(NETAESContext* context) {
    (void)context;
}

BOOL NETAESEncrypt(NETAESContext* context, void* dst, const void* src, s32 length) {
    if (length & 0xF) {
        return FALSE;
    }
    Aes* aes = reinterpret_cast<Aes*>(context);
    const u8* in = static_cast<const u8*>(src);
    u8* out = static_cast<u8*>(dst);
    for (s32 off = 0; off < length; off += 16) {
        u8 block[16];
        for (int i = 0; i < 16; i++) {
            block[i] = in[off + i] ^ aes->iv[i];
        }
        EncryptBlock(aes, block);
        std::memcpy(out + off, block, 16);
        std::memcpy(aes->iv, block, 16);
    }
    return TRUE;
}

BOOL NETAESDecrypt(NETAESContext* context, void* dst, const void* src, s32 length) {
    if (length & 0xF) {
        return FALSE;
    }
    Aes* aes = reinterpret_cast<Aes*>(context);
    const u8* in = static_cast<const u8*>(src);
    u8* out = static_cast<u8*>(dst);
    for (s32 off = 0; off < length; off += 16) {
        u8 cipher[16];
        std::memcpy(cipher, in + off, 16); // in place (dst == src) is allowed
        u8 block[16];
        std::memcpy(block, cipher, 16);
        DecryptBlock(aes, block);
        for (int i = 0; i < 16; i++) {
            out[off + i] = block[i] ^ aes->iv[i];
        }
        std::memcpy(aes->iv, cipher, 16);
    }
    return TRUE;
}

} // extern "C"

namespace cf::sdk {

void Sha1(const void* data, std::size_t size, std::uint8_t digest[20]) {
    ::Sha1 s;
    Sha1Init(&s);
    const u8* p = static_cast<const u8*>(data);
    while (size > 0) {
        const u32 chunk = size > 0x10000000 ? 0x10000000 : static_cast<u32>(size);
        Sha1Update(&s, p, chunk);
        p += chunk;
        size -= chunk;
    }
    Sha1Digest(&s, digest);
}

// MD5 (RFC 1321)
void Md5(const void* input, std::size_t size, std::uint8_t digest[16]) {
    const u8* data = static_cast<const u8*>(input);
    static const u32 K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const u8 S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                             5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                             4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                             6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    u32 h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    // The message padded to a multiple of 64 bytes: 0x80, zeros, the bit length (little-endian)
    const std::size_t padded = (size + 8) / 64 * 64 + 64;
    u8 block[64];
    for (std::size_t offset = 0; offset < padded; offset += 64) {
        for (u32 i = 0; i < 64; i++) {
            const std::size_t at = offset + i;
            block[i] = at < size ? data[at] : at == size ? 0x80 : 0;
        }
        if (offset + 64 == padded) {
            const u64 bits = static_cast<u64>(size) * 8;
            for (int i = 0; i < 8; i++) {
                block[56 + i] = static_cast<u8>(bits >> (i * 8));
            }
        }
        u32 m[16];
        for (int i = 0; i < 16; i++) {
            m[i] = u32(block[i * 4]) | (u32(block[i * 4 + 1]) << 8) | (u32(block[i * 4 + 2]) << 16) |
                   (u32(block[i * 4 + 3]) << 24);
        }
        u32 a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            u32 f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            const u32 t = d;
            d = c;
            c = b;
            const u32 x = a + f + K[i] + m[g];
            b = b + ((x << S[i]) | (x >> (32 - S[i])));
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            digest[i * 4 + j] = static_cast<u8>(h[i] >> (j * 8));
        }
    }
}


// AES-128 in OFB mode (WiiConnect24 downloads): data is decrypted (or encrypted) in place
void AesOfb(const std::uint8_t key[16], const std::uint8_t iv[16], std::uint8_t* data, std::size_t size) {
    Aes aes;
    AesExpandKey(&aes, key, 16);
    u8 stream[16];
    std::memcpy(stream, iv, 16);
    for (std::size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            EncryptBlock(&aes, stream);
        }
        data[i] ^= stream[i % 16];
    }
}

} // namespace cf::sdk
