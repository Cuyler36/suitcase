// WiiConnect24 Wii numbers (wc24_identity.h). Generation follows the KD's (documented from Dolphin's
// emulation of it); the unscrambling and CRC are the SDK's (lib/RevoEX/src/nwc24/NWC24UserId.c).
#include "wc24_identity.h"

namespace cf {
namespace sdk {
namespace wc24 {

namespace {

constexpr std::uint64_t kPolynomial = 0x635; // over the 53-bit ID, a 10-bit remainder
constexpr std::uint64_t kMask53 = 0x1FFFFFFFFFFFFFull;
const std::uint8_t kNibble[16] = {0x4, 0xB, 0x7, 0x9, 0xF, 0x1, 0xD, 0x3, 0xC, 0x2, 0x6, 0xE, 0x8, 0x0, 0xA, 0x5};
const std::uint8_t kNibbleInv[16] = {13, 5, 9, 7, 0, 15, 10, 2, 12, 3, 14, 1, 8, 6, 11, 4};
// Generation moves byte i to kBytePos[i]; unscrambling takes byte i from kBytePos[i]
const std::uint8_t kBytePos[8] = {1, 5, 0, 4, 2, 3, 6, 7};

std::uint8_t GetByte(std::uint64_t v, int i) {
    return static_cast<std::uint8_t>(v >> (i * 8));
}

std::uint64_t SetByte(std::uint64_t v, int i, std::uint8_t b) {
    return (v & ~(0xFFull << (i * 8))) | (static_cast<std::uint64_t>(b) << (i * 8));
}

// The remainder of the 53-bit value under the polynomial (0: the CRC holds)
std::uint64_t Remainder(std::uint64_t v) {
    for (int i = 0; i < 43; i++) {
        if ((v >> (52 - i)) & 1) {
            v ^= kPolynomial << (42 - i);
        }
    }
    return v;
}

} // namespace

std::uint64_t GenerateWiiNumber(std::uint32_t consoleId, std::uint32_t counter, Model model, Area area) {
    const std::uint64_t fields = (static_cast<std::uint64_t>(area) << 50) | (static_cast<std::uint64_t>(model) << 47) |
                                 (static_cast<std::uint64_t>(consoleId) << 15) |
                                 (static_cast<std::uint64_t>(counter & 0x1F) << 10);
    std::uint64_t mix = (fields | (Remainder(fields) & 0xFFFFFFFF)) ^ 0x0000B3B3B3B3B3B3ull;
    mix = (mix >> 10) | ((mix & 0x3FF) << 43);
    for (int i = 0; i < 6; i++) {
        const std::uint8_t b = GetByte(mix, i);
        mix = SetByte(mix, i, static_cast<std::uint8_t>((kNibble[b >> 4] << 4) | kNibble[b & 0xF]));
    }
    const std::uint64_t snapshot = mix;
    for (int i = 0; i < 6; i++) {
        mix = SetByte(mix, kBytePos[i], GetByte(snapshot, i));
    }
    mix &= kMask53;
    mix = ((mix << 1) | ((mix >> 52) & 1)) & kMask53;
    mix ^= 0x00005E5E5E5E5E5Eull;
    return mix & kMask53;
}

std::uint64_t UnscrambleWiiNumber(std::uint64_t v) {
    v &= kMask53;
    v ^= 0x5E5E5E5E5E5Eull;
    v &= kMask53;
    v |= (((v & 0xFF) << 5) & 0x20) << 48;
    v >>= 1;
    const std::uint64_t snapshot = v;
    for (int i = 0; i < 6; i++) {
        v = SetByte(v, i, GetByte(snapshot, kBytePos[i]));
    }
    for (int i = 0; i < 6; i++) {
        const std::uint8_t b = GetByte(v, i);
        v = SetByte(v, i, static_cast<std::uint8_t>((kNibbleInv[b >> 4] << 4) | kNibbleInv[b & 0xF]));
    }
    v = ((v & 0x7FFFFFFFFFFull) << 10) | ((v >> 43) & 0x3FF);
    v ^= 0xB3B3B3B3B3B3ull;
    return v;
}

bool CheckWiiNumber(std::uint64_t id) {
    return id <= kMaxWiiNumber && Remainder(UnscrambleWiiNumber(id)) == 0;
}

} // namespace wc24
} // namespace sdk
} // namespace cf
