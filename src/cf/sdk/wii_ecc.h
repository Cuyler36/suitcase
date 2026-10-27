// The Wii's elliptic curve signatures: ECDSA over sect233r1 (the curve of the console's NG key and the
// certificates IOS makes from it), with SHA-1 hashes. Keys and points are big-endian bytes as in the
// Wii's certificates: a private key or field element is 30 bytes, a public key (point) 60 (x then y).
// Following segher's ec.c (wii.git), which IOS's results and WiiLink's server (gpcm/ecdsa.go) share.
#pragma once

#include <array>
#include <cstdint>

namespace cf {
namespace sdk {
namespace ecc {

using PrivateKey = std::array<std::uint8_t, 30>;
using PublicKey = std::array<std::uint8_t, 60>;
using Signature = std::array<std::uint8_t, 60>; // r then s, 30 bytes each

// The public key of a private key (its multiple of the curve's base point)
PublicKey MakePublicKey(const PrivateKey& privateKey);

// A new random private key in [1, n - 1]
PrivateKey MakePrivateKey();

// The signature of a SHA-1 digest
Signature Sign(const PrivateKey& privateKey, const std::uint8_t digest[20]);

// Whether the signature of a SHA-1 digest is the key's
bool Verify(const PublicKey& publicKey, const Signature& signature, const std::uint8_t digest[20]);

} // namespace ecc
} // namespace sdk
} // namespace cf
