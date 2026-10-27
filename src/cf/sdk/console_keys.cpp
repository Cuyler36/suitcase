// The console's keys and the WiiLink login signature (console_keys.h).
//
// device.cert (0x180 bytes, big-endian): 0x00 signature type (0x10002: ECC), 0x04 Nintendo's signature
// (60 bytes), 0x80 issuer ("Root-CA%08x-MS%08x"), 0xC0 key type (2: ECC), 0xC4 name ("NG%08x"),
// 0x104 key ID, 0x108 public key (60 bytes). The signature covers SHA-1 of 0x80..0x180.
#include "console_keys.h"

#include "nand_fs.h"
#include "net_crypto.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace cf {
namespace sdk {

namespace {

constexpr std::size_t kCertIssuer = 0x80;
constexpr std::size_t kCertKeyType = 0xC0;
constexpr std::size_t kCertName = 0xC4;
constexpr std::size_t kCertKeyId = 0x104;
constexpr std::size_t kCertPublicKey = 0x108;

std::uint32_t LoadBE32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void StoreBE32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

bool ReadHostFile(const std::filesystem::path& path, std::vector<std::uint8_t>* out) {
    std::FILE* f = OpenHostFile(path, "rb");
    if (f == nullptr) {
        return false;
    }
    out->clear();
    std::uint8_t buf[1024];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out->insert(out->end(), buf, buf + n);
    }
    std::fclose(f);
    return true;
}

// keys.txt: "name = hex bytes" lines
std::map<std::string, std::vector<std::uint8_t>> ParseKeys(const std::vector<std::uint8_t>& text) {
    std::map<std::string, std::vector<std::uint8_t>> keys;
    std::string line;
    auto flush = [&] {
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string name = line.substr(0, eq);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
                name.pop_back();
            }
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) {
                name.erase(name.begin());
            }
            std::vector<std::uint8_t> bytes;
            int nibbles = 0;
            std::uint8_t byte = 0;
            for (std::size_t i = eq + 1; i < line.size(); i++) {
                const char c = line[i];
                if (!std::isxdigit(static_cast<unsigned char>(c))) {
                    continue;
                }
                byte = static_cast<std::uint8_t>((byte << 4) | (std::isdigit(static_cast<unsigned char>(c))
                                                                     ? c - '0'
                                                                     : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
                if (++nibbles % 2 == 0) {
                    bytes.push_back(byte);
                    byte = 0;
                }
            }
            keys[name] = std::move(bytes);
        }
        line.clear();
    };
    for (std::uint8_t c : text) {
        if (c == '\n' || c == '\r') {
            flush();
        } else {
            line.push_back(static_cast<char>(c));
        }
    }
    flush();
    return keys;
}

// A certificate body (from its issuer on): issuer, key type 2 (ECC), name, key ID, public key, padding
std::array<std::uint8_t, 0x100> CertBody(const std::string& issuer, const std::string& name, std::uint32_t keyId,
                                         const ecc::PublicKey& publicKey) {
    std::array<std::uint8_t, 0x100> body{};
    std::memcpy(body.data(), issuer.data(), std::min<std::size_t>(issuer.size(), 0x40));
    StoreBE32(body.data() + 0x40, 2);
    std::memcpy(body.data() + 0x44, name.data(), std::min<std::size_t>(name.size(), 0x40));
    StoreBE32(body.data() + 0x84, keyId);
    std::memcpy(body.data() + 0x88, publicKey.data(), publicKey.size());
    return body;
}

std::string CStr(const std::uint8_t* p, std::size_t max) {
    std::size_t n = 0;
    while (n < max && p[n] != 0) {
        n++;
    }
    return std::string(reinterpret_cast<const char*>(p), n);
}

} // namespace

std::optional<ConsoleKeys> LoadConsoleKeys(std::string* error) {
    auto fail = [&](const char* reason) -> std::optional<ConsoleKeys> {
        if (error != nullptr) {
            *error = reason;
        }
        return std::nullopt;
    };
    const std::filesystem::path dir = UserDirPath() / "console";
    std::vector<std::uint8_t> text, cert;
    if (!ReadHostFile(dir / "keys.txt", &text) || !ReadHostFile(dir / "device.cert", &cert)) {
        return fail("no console/keys.txt and console/device.cert in the user directory");
    }
    const auto keys = ParseKeys(text);
    const auto get = [&](const char* name, std::size_t size) -> const std::vector<std::uint8_t>* {
        const auto it = keys.find(name);
        return it != keys.end() && it->second.size() == size ? &it->second : nullptr;
    };
    const auto* consoleId = get("console_id", 4);
    const auto* privateKey = get("ecc_private_key", 30);
    const auto* mac = get("mac_address", 6);
    if (consoleId == nullptr || privateKey == nullptr || mac == nullptr) {
        return fail("keys.txt lacks console_id, ecc_private_key or mac_address");
    }
    if (cert.size() != 0x180 || LoadBE32(cert.data()) != 0x10002 || LoadBE32(cert.data() + kCertKeyType) != 2) {
        return fail("device.cert is not an ECC device certificate");
    }
    ConsoleKeys result;
    result.ngId = LoadBE32(consoleId->data());
    std::memcpy(result.ngPrivateKey.data(), privateKey->data(), 30);
    std::memcpy(result.macAddress.data(), mac->data(), 6);
    std::memcpy(result.certificate.data(), cert.data(), 0x180);

    char name[16];
    std::snprintf(name, sizeof(name), "ng%08x", result.ngId);
    std::string certName = CStr(cert.data() + kCertName, 0x40);
    for (char& c : certName) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (certName != name) {
        return fail("device.cert belongs to another console than keys.txt");
    }
    ecc::PublicKey certKey;
    std::memcpy(certKey.data(), cert.data() + kCertPublicKey, 60);
    if (ecc::MakePublicKey(result.ngPrivateKey) != certKey) {
        return fail("keys.txt's private key is not device.cert's");
    }
    return result; // Nintendo's signature of the certificate is the server's to check
}

std::vector<std::uint8_t> MakeLoginSignature(const ConsoleKeys& keys, const std::string& authToken, std::uint64_t titleId) {
    const std::uint8_t* cert = keys.certificate.data();
    const std::string ngIssuer = CStr(cert + kCertIssuer, 0x40);
    unsigned caId = 0, msId = 0;
    std::sscanf(ngIssuer.c_str(), "Root-CA%08x-MS%08x", &caId, &msId);
    char ngName[16];
    std::snprintf(ngName, sizeof(ngName), "NG%08x", keys.ngId);

    // The AP certificate: a new key, named after the title, signed by the NG key
    const ecc::PrivateKey apPrivateKey = ecc::MakePrivateKey();
    const ecc::PublicKey apPublicKey = ecc::MakePublicKey(apPrivateKey);
    char apName[24];
    std::snprintf(apName, sizeof(apName), "AP%016llx", static_cast<unsigned long long>(titleId));
    const auto apBody = CertBody(ngIssuer + "-" + ngName, apName, 0, apPublicKey);
    std::uint8_t digest[20];
    Sha1(apBody.data(), apBody.size(), digest);
    const ecc::Signature ngSignature = ecc::Sign(keys.ngPrivateKey, digest);

    // The token, signed by the AP key
    Sha1(authToken.data(), authToken.size(), digest);
    const ecc::Signature apSignature = ecc::Sign(apPrivateKey, digest);

    std::vector<std::uint8_t> sig(0x144);
    std::uint8_t* p = sig.data();
    StoreBE32(p + 0x000, keys.ngId);
    std::memcpy(p + 0x004, cert + kCertKeyId, 4);
    StoreBE32(p + 0x008, caId);
    StoreBE32(p + 0x00C, msId);
    for (int i = 0; i < 8; i++) {
        p[0x010 + i] = static_cast<std::uint8_t>(titleId >> (56 - i * 8));
    }
    std::memcpy(p + 0x018, cert + 4, 60);                  // Nintendo's signature of the NG certificate
    std::memcpy(p + 0x054, cert + kCertPublicKey, 60);     // the NG public key
    std::memcpy(p + 0x090, ngSignature.data(), 60);
    std::memcpy(p + 0x0CC, apPublicKey.data(), 60);
    std::memcpy(p + 0x108, apSignature.data(), 60);
    return sig;
}

std::string EncodeDwcBase64(const std::vector<std::uint8_t>& data) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::uint32_t n = (std::uint32_t(data[i]) << 16) | (i + 1 < data.size() ? data[i + 1] << 8 : 0) |
                                (i + 2 < data.size() ? data[i + 2] : 0);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? kAlphabet[(n >> 6) & 63] : '*');
        out.push_back(i + 2 < data.size() ? kAlphabet[n & 63] : '*');
    }
    return out;
}

} // namespace sdk
} // namespace cf
