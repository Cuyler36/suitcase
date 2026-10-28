// The WiiLink WFC account login (cf/wfc.h): what the game's DWC does at "connect", with WiiLink's
// console signature added as its patch adds it (wfc-patcher-wii payload/wwfcLogin.cpp).
//
// 1. NAS (http://nas.<service>/ac, WiiLink's patch's address for naswii.nintendowifi.net): a form
//    whose values are DWC Base64 (lib/dwc/auth/dwc_auth_interface.c), answered with a login token
//    and a challenge.
// 2. GPCM (gpcm.gs.<service>:29900, GameSpy presence): the server's challenge, then the GameSpy
//    login (lib/gamespy/GP/gpiConnect.c gpiSendLogin) with the token, the challenge response and
//    WiiLink's fields: profileid, wl:ver, wl:sig (console_keys.h) and wl:host.
//
// The login names the player as DWC does: their authentic login ID once they have one, else the
// console's DWC user ID with their pseudo login ID's player ID. A profile ID in the record (from a
// Nintendo or WiiLink login) is sent so the account keeps it; the port's own IDs never are (WiiLink
// reserves their range).
#include <cf/online.h>
#include <cf/version_config.h>
#include <cf/wfc.h>

#include "console_keys.h"
#include "host_net.h"
#include "net_crypto.h"
#include "wc24_console.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace cf {
namespace wfc {

namespace {

constexpr std::uint16_t kGpcmPort = 29900;
constexpr const char* kGameName = "acrossingwii"; // DWC_InitFriendsMatch's (d_net_dwc.cpp)
constexpr int kProductId = 0x2CC0;                // dNetDwc::GAME_PRODUCT_ID
constexpr int kNamespaceId = 16;                  // DWC's gpInitialize (dwc_main.c)
constexpr int kPartnerId = 11;
constexpr int kSdkRevision = 3;                   // GPI_SDKREV
// The lowest payload version WiiLink accepts (gpcm/login.go MinimumPayloadVersions: 0.1):
// major << 24 | minor << 12 | beta
constexpr int kPayloadVersion = (0 << 24) | (1 << 12);
constexpr const char* kHostPlatform = "Suitcase";
// A profile ID a login may claim: WiiLink refuses 1,000,000,000 and up (database/user.go:
// ErrReservedProfileIDRange), the range it assigns its own accounts from. An account it assigned one in is
// found by user ID and gsbrcd instead, so the login leaves the field out.
constexpr std::uint32_t kClaimableProfileIdLimit = 1000000000;

// ---------------------------------------------------------------------------------------------
// Encodings
// ---------------------------------------------------------------------------------------------
std::string DwcBase64(const std::string& text) {
    return sdk::EncodeDwcBase64(std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::string DwcBase64Decode(const std::string& text) {
    static const std::string kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-";
    std::string out;
    std::uint32_t bits = 0;
    int count = 0;
    for (char c : text) {
        const std::size_t v = kAlphabet.find(c);
        if (v == std::string::npos) {
            continue; // '*' padding
        }
        bits = (bits << 6) | static_cast<std::uint32_t>(v);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(static_cast<char>((bits >> count) & 0xFF));
        }
    }
    return out;
}

std::string UrlDecode(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] == '%' && i + 2 < text.size()) {
            out.push_back(static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(text[i] == '+' ? ' ' : text[i]);
        }
    }
    return out;
}

// MD5 as lowercase hex (GameSpy's challenge response)
std::string Md5Hex(const std::string& data) {
    std::uint8_t digest[16];
    sdk::Md5(data.data(), data.size(), digest);
    char out[33];
    for (int i = 0; i < 16; i++) {
        std::snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(out, 32);
}

// The 43-bit user ID and the player ID of a login ID (3 big-endian words)
struct LoginId {
    std::uint64_t userId = 0;
    std::uint32_t playerId = 0;
    bool valid = false; // its type is a login ID
};

std::uint32_t LoadBE32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

LoginId ReadLoginId(const std::uint8_t* p) {
    LoginId id;
    const std::uint32_t word = LoadBE32(p);
    id.valid = ((word >> 11) & 3) == 1;
    id.userId = (static_cast<std::uint64_t>(word & 0x7FF) << 32) | LoadBE32(p + 4);
    id.playerId = LoadBE32(p + 8);
    return id;
}

// DWC's base-32 digits ("0".."9", "a".."v") of the low bits of a number
std::string Base32(std::uint64_t value, int bits) {
    static const char kDigits[] = "0123456789abcdefghijklmnopqrstuv";
    const int count = (bits + 4) / 5;
    std::string out(count, '0');
    for (int i = count - 1; i >= 0; i--) {
        out[i] = kDigits[value & 31];
        value >>= 5;
    }
    return out;
}

// The GameSpy message's \key\value pairs
std::map<std::string, std::string> ParseGameSpy(const std::string& message) {
    std::map<std::string, std::string> values;
    std::size_t pos = 0;
    while (pos < message.size() && message[pos] == '\\') {
        const std::size_t keyEnd = message.find('\\', pos + 1);
        if (keyEnd == std::string::npos) {
            break;
        }
        const std::string key = message.substr(pos + 1, keyEnd - pos - 1);
        std::size_t valueEnd = message.find('\\', keyEnd + 1);
        if (valueEnd == std::string::npos) {
            valueEnd = message.size();
        }
        if (key == "final") {
            break;
        }
        if (values.find(key) == values.end()) {
            values[key] = message.substr(keyEnd + 1, valueEnd - keyEnd - 1);
        }
        pos = valueEnd;
    }
    return values;
}

std::string RandomChallenge(int length) {
    static const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device random;
    std::string out;
    for (int i = 0; i < length; i++) {
        out.push_back(kChars[random() % (sizeof(kChars) - 1)]);
    }
    return out;
}

// The region and language codes of the game's region (SCGetProductArea, SCGetLanguage)
void RegionOfGame(const char** region, const char** language) {
    switch (CF_GAME_ID[3]) {
    case 'J':
        *region = "00";
        *language = "00";
        break;
    case 'P':
        *region = "02";
        *language = "01";
        break;
    case 'K':
        *region = "04";
        *language = "09";
        break;
    default:
        *region = "01";
        *language = "01";
        break;
    }
}

} // namespace

LoginResult TestLogin(const std::uint8_t userData[0x40]) {
    LoginResult result;
    auto step = [&](const std::string& line) { result.log += line + "\n"; };
    auto fail = [&](const std::string& why) {
        result.error = why;
        step("FAILED: " + why);
        return result;
    };

    std::string keyError;
    const auto keys = sdk::LoadConsoleKeys(&keyError);
    if (!keys) {
        return fail(keyError);
    }
    char line[256];
    std::snprintf(line, sizeof(line), "console keys: NG%08x, checked", keys->ngId);
    step(line);
    const online::ServiceInfo& service = online::Info(online::Current());
    const std::string nasHost = std::string("nas.") + service.wfcDomain;
    const std::string gpcmHost = std::string("gpcm.gs.") + service.wfcDomain;
    step(std::string("service: ") + service.name);

    // The player: DWC's login names
    const std::uint32_t gameCode = LoadBE32(userData + 0x24);
    const LoginId authentic = ReadLoginId(userData + 0x10);
    const LoginId pseudo = ReadLoginId(userData + 0x04);
    const int recordProfileId = static_cast<int>(LoadBE32(userData + 0x1C));
    const bool hasAuthentic = authentic.valid;
    const std::uint64_t userId = hasAuthentic ? authentic.userId : sdk::wc24::ConsoleDwcUserId();
    const std::uint32_t playerId = hasAuthentic ? authentic.playerId : pseudo.playerId;
    std::string gsbrcd;
    for (int i = 3; i >= 0; i--) {
        gsbrcd.push_back(static_cast<char>(gameCode >> (i * 8)));
    }
    gsbrcd += Base32(playerId, 32);
    const int sendProfileId =
        recordProfileId > 0 && static_cast<std::uint32_t>(recordProfileId) < kClaimableProfileIdLimit ? recordProfileId : 0;
    std::snprintf(line, sizeof(line), "player: %s login ID, gsbrcd %s, profile ID sent %d",
                  hasAuthentic ? "authentic" : "pseudo", gsbrcd.c_str(), sendProfileId);
    step(line);

    // 1. NAS login
    const char* region;
    const char* language;
    RegionOfGame(&region, &language);
    char userIdText[32], wiiNumber[32], mac[16], devtime[16];
    std::snprintf(userIdText, sizeof(userIdText), "%013llu", static_cast<unsigned long long>(userId));
    std::snprintf(wiiNumber, sizeof(wiiNumber), "%016llu",
                  static_cast<unsigned long long>(sdk::wc24::ConsoleWiiNumber()));
    std::snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x", keys->macAddress[0], keys->macAddress[1],
                  keys->macAddress[2], keys->macAddress[3], keys->macAddress[4], keys->macAddress[5]);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::strftime(devtime, sizeof(devtime), "%y%m%d%H%M%S", &local);
    const std::string gameId(CF_GAME_ID, 4);
    const std::pair<const char*, std::string> fields[] = {
        {"action", "login"},   {"gsbrcd", gsbrcd}, {"userid", userIdText}, {"ingamesn", ""},
        {"sdkver", "001000"},  {"gamecd", gameId}, {"makercd", "01"},      {"unitcd", "1"},
        {"macadr", mac},       {"lang", language}, {"devtime", devtime},   {"cfc", wiiNumber},
        {"region", region},
    };
    std::string body;
    for (const auto& [key, value] : fields) {
        body += (body.empty() ? "" : "&") + std::string(key) + "=" + DwcBase64(value);
    }
    std::string token, nasChallenge;
    {
        const sdk::net::HttpResponse response =
            sdk::net::HttpRequest("http://" + nasHost + "/ac", "", "application/x-www-form-urlencoded", body,
                                  "HTTP_X_GAMECD: " + gameId + "\r\n");
        if (response.status == 0) {
            return fail("NAS: " + response.error);
        }
        step("NAS: HTTP " + std::to_string(response.status));
        if (response.status != 200) {
            return fail("NAS: HTTP " + std::to_string(response.status));
        }
        const std::string replyBody = response.body.substr(0, response.body.find('\0'));
        std::map<std::string, std::string> reply;
        std::size_t pos = 0;
        while (pos < replyBody.size()) {
            std::size_t end = replyBody.find('&', pos);
            if (end == std::string::npos) {
                end = replyBody.size();
            }
            const std::string pair = replyBody.substr(pos, end - pos);
            const std::size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                reply[pair.substr(0, eq)] = DwcBase64Decode(UrlDecode(pair.substr(eq + 1)));
            }
            pos = end + 1;
        }
        const std::string returnCode = reply["returncd"];
        step("NAS: returncd " + returnCode);
        if (returnCode != "001" && returnCode != "040") {
            return fail("NAS refused the login (returncd " + returnCode + ")");
        }
        token = reply["token"];
        nasChallenge = reply["challenge"];
        if (token.empty() || nasChallenge.empty()) {
            return fail("NAS: no token");
        }
        step("NAS: login token received");
    }

    // 2. GPCM login
    sdk::net::Socket gpcm;
    if (!gpcm.Connect(gpcmHost, kGpcmPort)) {
        return fail("cannot connect to " + gpcmHost);
    }
    std::string greeting;
    if (!gpcm.ReceiveUntil(&greeting, "\\final\\")) {
        return fail("GPCM: no greeting");
    }
    const auto hello = ParseGameSpy(greeting);
    const auto serverChallenge = hello.find("challenge");
    if (serverChallenge == hello.end()) {
        return fail("GPCM: no challenge in the greeting");
    }
    step("GPCM: connected");
    const std::string userChallenge = RandomChallenge(32);
    const std::string nasHash = Md5Hex(nasChallenge);
    const std::string response = Md5Hex(nasHash + std::string(48, ' ') + token + userChallenge +
                                        serverChallenge->second + nasHash);
    // The AP certificate names the running title: 00010000 and the game ID's first four letters
    const std::uint64_t titleId =
        0x0001000000000000ull | LoadBE32(reinterpret_cast<const std::uint8_t*>(CF_GAME_ID));
    const std::string signature = sdk::EncodeDwcBase64(sdk::MakeLoginSignature(*keys, token, titleId));
    std::string login = "\\login\\\\challenge\\" + userChallenge + "\\authtoken\\" + token + "\\partnerid\\" +
                        std::to_string(kPartnerId) + "\\response\\" + response + "\\firewall\\1\\port\\0\\productid\\" +
                        std::to_string(kProductId) + "\\gamename\\" + kGameName + "\\namespaceid\\" +
                        std::to_string(kNamespaceId) + "\\sdkrevision\\" + std::to_string(kSdkRevision) +
                        "\\quiet\\0\\id\\1";
    if (sendProfileId != 0) {
        login += "\\profileid\\" + std::to_string(sendProfileId);
    }
    login += "\\wl:ver\\" + std::to_string(kPayloadVersion) + "\\wl:sig\\" + signature + "\\wl:host\\" +
             kHostPlatform + "\\final\\";
    std::string reply;
    if (!gpcm.SendAll(login) || !gpcm.ReceiveUntil(&reply, "\\final\\")) {
        return fail("GPCM: no reply to the login");
    }
    const auto values = ParseGameSpy(reply);
    if (values.count("error") != 0 || values.count("profileid") == 0) {
        const auto message = values.find("errmsg");
        const auto code = values.find("err");
        return fail("GPCM refused the login: " + (message != values.end() ? message->second : reply.substr(0, 200)) +
                    (code != values.end() ? " (error " + code->second + ")" : ""));
    }
    result.profileId = std::stoi(values.at("profileid"));
    result.friendCode = FriendCodeOf(result.profileId, gameCode);
    result.ok = true;
    std::snprintf(line, sizeof(line), "GPCM: logged in as profile %d, friend code %04llu-%04llu-%04llu",
                  result.profileId, static_cast<unsigned long long>(result.friendCode / 100000000),
                  static_cast<unsigned long long>(result.friendCode / 10000 % 10000),
                  static_cast<unsigned long long>(result.friendCode % 10000));
    step(line);

    const auto sessionKey = values.find("sesskey");
    if (sessionKey != values.end()) {
        gpcm.SendAll("\\logout\\\\sesskey\\" + sessionKey->second + "\\final\\");
        step("GPCM: logged out");
    }
    return result;
}

} // namespace wfc

} // namespace cf

// ---------------------------------------------------------------------------------------------
// The game's own login (GameSpy's gpiSendLogin, lib/gamespy/GP/gpiConnect.c): the fields WiiLink's
// patch adds before \final\, as its payload/wwfcLogin.cpp SendExtendedLogin does
// ---------------------------------------------------------------------------------------------
extern "C" void* DWCi_GetUserData(void); // lib/dwc/match/dwc_login.c: the player's DWCAccUserData

extern "C" const char* CF_WfcLoginFields(const char* authToken, int gpProfileId) {
    static thread_local std::string fields;
    fields.clear();
    std::string error;
    const auto keys = cf::sdk::LoadConsoleKeys(&error);
    if (!keys) {
        return ""; // no console keys: WiiLink refuses the login (the port's own players do not log in)
    }
    // The profile ID the record holds (0x1C in DWCAccUserData), so the account keeps it, unless GameSpy
    // already sent its own
    const void* userData = gpProfileId == 0 ? DWCi_GetUserData() : nullptr;
    if (userData != nullptr) {
        std::int32_t profileId;
        std::memcpy(&profileId, static_cast<const std::uint8_t*>(userData) + 0x1C, sizeof(profileId));
        if (profileId > 0 && static_cast<std::uint32_t>(profileId) < cf::wfc::kClaimableProfileIdLimit) {
            fields += "\\profileid\\" + std::to_string(profileId);
        }
    }
    fields += "\\wl:ver\\" +std::to_string(cf::wfc::kPayloadVersion);
    if (authToken != nullptr && authToken[0] != '\0') {
        const std::uint8_t* id = reinterpret_cast<const std::uint8_t*>(CF_GAME_ID);
        const std::uint64_t titleId = 0x0001000000000000ull | (std::uint64_t(id[0]) << 24) | (std::uint64_t(id[1]) << 16) |
                                      (std::uint64_t(id[2]) << 8) | id[3];
        fields += "\\wl:sig\\" +cf::sdk::EncodeDwcBase64(cf::sdk::MakeLoginSignature(*keys, authToken, titleId));
    }
    fields += "\\wl:host\\";
    fields += cf::wfc::kHostPlatform;
    return fields.c_str();
}
