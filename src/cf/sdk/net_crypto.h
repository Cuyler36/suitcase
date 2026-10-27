// Hashes of net_crypto_revoex.cpp for the host code (outside the RevoEX NET API)
#pragma once

#include <cstddef>
#include <cstdint>

namespace cf {
namespace sdk {

// SHA-1 (FIPS 180-4)
void Sha1(const void* data, std::size_t size, std::uint8_t digest[20]);

// MD5 (RFC 1321)
void Md5(const void* data, std::size_t size, std::uint8_t digest[16]);

// AES-128 in OFB mode, in place (decryption and encryption are the same)
void AesOfb(const std::uint8_t key[16], const std::uint8_t iv[16], std::uint8_t* data, std::size_t size);

} // namespace sdk
} // namespace cf
