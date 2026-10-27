// The RevoEX NWC24 library (revolution/nwc24.h) on the host: WiiConnect24 for the calls the game makes.
// On the Wii the SDK writes mail into VFF mailboxes and the system's KD service exchanges them with
// the servers; here the mailboxes are folders under the NAND's /shared2/wc24, written in the SDK's
// own message format (wc24_mail.h), and the transport moves the files.
//
// Layout (NAND paths; nand_fs.cpp maps them into the user directory):
//   /shared2/wc24/identity.json        this console's Wii number (wc24_identity.h) and its account
//   /shared2/wc24/secret_friends.json  NWC24AddSecretFriendInfo: the application's friends
//   /shared2/wc24/dl_tasks.json        the download tasks
//   /shared2/wc24/outbox/s<id>.msg     committed messages, as the SDK writes them to its send box
//   /shared2/wc24/inbox/r<id>.msg      received messages
//   <title home>/wc24dl.vff/           the title's download box (a folder; VF mounts it)
//   <title home>/wc24pubk.mod          the download keys (NWC24SetDlKeys), as on the Wii
//
// Transport: the online service's mail server (cf/online.h; wc24_mail_client.h), on a worker thread.
// CF_WC24_LOOPBACK=<folder> instead delivers committed messages into <folder>/<Wii number>/ and
// collects this console's from there (a test between game instances, without a server).
//
// Not implemented: the Wii Address Book (the Message Board's friends; none, as on a console where
// none were registered), and Message Board / e-mail messages, which are accepted and dropped.
#include <revolution/nwc24.h>
#include <private/nwc24/NWC24MsgCommit.h>
#include <private/nwc24/NWC24UserId.h>

#include <borealis/log_c.h>
#include <cf/online.h>
#include <cf/wc24.h>

#include <nlohmann/json.hpp>

#include "cf/version_config.h"
#include "console_keys.h"
#include "host_net.h"
#include "nand_fs.h"
#include "net_crypto.h"
#include "wc24_console.h"
#include "wc24_identity.h"
#include "wc24_mail.h"
#include "wc24_mail_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;
using namespace cf::sdk::wc24;

namespace {

#define LOG(...) borealis_log_printf(BOREALIS_LOG_INFO, "cf::wc24", __VA_ARGS__)
#define WARN(...) borealis_log_printf(BOREALIS_LOG_WARNING, "cf::wc24", __VA_ARGS__)

// Console IDs the port makes (without the player's console keys) are in the top half of the 32-bit
// range, which no Nintendo console uses (NG IDs are far below: 0x02000001.., Wii U vWii 0x24..): a port
// console cannot take a real Wii's Wii number, and is recognizable as one (docs/wc24/README.md)
constexpr std::uint32_t kPortConsoleIdBase = 0x80000000;
constexpr std::size_t kMaxSecretFriends = 100;
constexpr std::size_t kMaxAttachments = 2; // NWC24i_MSG_ATTACHMENT_MAX
constexpr std::size_t kMaxRecipients = 8;  // NWC24i_MSG_RECIPIENT_MAX

std::recursive_mutex sMutex;
bool sOpened = false;

// ---------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------
fs::path Wc24Dir() {
    return cf::sdk::NandHostPath("/shared2/wc24");
}

std::string GameId() {
    return CF_GAME_ID;
}

std::uint32_t AppId() {
    const std::string id = GameId();
    return (std::uint32_t(std::uint8_t(id[0])) << 24) | (std::uint32_t(std::uint8_t(id[1])) << 16) |
           (std::uint32_t(std::uint8_t(id[2])) << 8) | std::uint8_t(id[3]);
}

// The disc's maker code ("01": Nintendo), the SDK's group id for a disc game
std::uint16_t GroupId() {
    return ('0' << 8) | '1';
}

Area AreaOfGame() {
    switch (GameId()[3]) {
    case 'J':
        return AREA_JPN;
    case 'P':
        return AREA_EUR;
    case 'K':
        return AREA_KOR;
    default:
        return AREA_USA;
    }
}

bool ReadFile(const fs::path& path, std::string* out) {
    std::FILE* f = cf::sdk::OpenHostFile(path, "rb");
    if (f == nullptr) {
        return false;
    }
    out->clear();
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out->append(buf, n);
    }
    std::fclose(f);
    return true;
}

// Written through a temporary file, so a crash leaves the old or the new file
bool WriteFile(const fs::path& path, const std::string& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    std::FILE* f = cf::sdk::OpenHostFile(tmp, "wb");
    if (f == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

json ReadJson(const fs::path& path, json fallback) {
    std::string text;
    if (!ReadFile(path, &text)) {
        return fallback;
    }
    json j = json::parse(text, nullptr, false);
    return j.is_discarded() ? fallback : j;
}

bool WriteJson(const fs::path& path, const json& j) {
    return WriteFile(path, j.dump(2));
}

// ---------------------------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------------------------
struct Identity {
    std::uint32_t consoleId = 0;
    std::uint32_t counter = 0;
    std::uint64_t wiiNumber = 0;
    std::uint32_t nextMessageId = 1;
    std::uint64_t dwcUserId = 0; // ConsoleDwcUserId
    std::string mailPassword;    // the mail account (account.cgi's passwd and mlchkid)
    std::string mailCheckId;
};

Identity sIdentity;
bool sIdentityLoaded = false;

void SaveIdentity() {
    json j = ReadJson(Wc24Dir() / "identity.json", json::object());
    j["consoleId"] = sIdentity.consoleId;
    j["counter"] = sIdentity.counter;
    char number[17];
    std::snprintf(number, sizeof(number), "%016llu", static_cast<unsigned long long>(sIdentity.wiiNumber));
    j["wiiNumber"] = number;
    j["nextMessageId"] = sIdentity.nextMessageId;
    j["dwcUserId"] = sIdentity.dwcUserId;
    if (!sIdentity.mailPassword.empty()) {
        j["mailPassword"] = sIdentity.mailPassword;
        j["mailCheckId"] = sIdentity.mailCheckId;
    }
    if (!WriteJson(Wc24Dir() / "identity.json", j)) {
        WARN("cannot write identity.json");
    }
}

// A random DWC user ID (43 bits, not 0), as the Wii's DWC makes at the console's first use
std::uint64_t MakeDwcUserId(std::mt19937_64& rng) {
    std::uint64_t id;
    do {
        id = rng() & ((1ull << 43) - 1);
    } while (id == 0);
    return id;
}

// The console ID of the player's console keys (console_keys.h), 0 without them
std::uint32_t KeysConsoleId() {
    std::string error;
    const std::optional<cf::sdk::ConsoleKeys> keys = cf::sdk::LoadConsoleKeys(&error);
    return keys ? keys->ngId : 0;
}

// The console ID and its Wii number (the first counter whose number checks out, as the KD makes it):
// the console keys' NG ID, which gives the console's own Wii number, else a new one in the port's range
void MakeConsoleIdentity(std::uint32_t keysConsoleId) {
    std::random_device rd;
    std::mt19937 rng(rd());
    sIdentity.consoleId = keysConsoleId != 0
                              ? keysConsoleId
                              : kPortConsoleIdBase | (static_cast<std::uint32_t>(rng()) & ~kPortConsoleIdBase);
    sIdentity.counter = 0;
    sIdentity.wiiNumber = GenerateWiiNumber(sIdentity.consoleId, sIdentity.counter, MODEL_RVL, AreaOfGame());
    while (!CheckWiiNumber(sIdentity.wiiNumber) && sIdentity.counter < 31) {
        sIdentity.counter++;
        sIdentity.wiiNumber = GenerateWiiNumber(sIdentity.consoleId, sIdentity.counter, MODEL_RVL, AreaOfGame());
    }
    LOG("new Wii number %016llu (console ID %08X)", static_cast<unsigned long long>(sIdentity.wiiNumber),
        sIdentity.consoleId);
}

// The console's own WiiConnect24 settings (its NAND's /shared2/wc24/nwc24msg.cfg, which the player puts
// beside the console keys): the Wii number, its generation counter and, once registered, the mail
// account. 0x400 bytes, big-endian: magic "WcCf", version 8, Wii number at 0x08, the number of generations
// at 0x10, stage at 0x14 (2: registered), password at 0x58, mlchkid at 0x78, checksum (the sum of the other
// words) at 0x3FC.
struct ConsoleMailConfig {
    std::uint64_t wiiNumber = 0;
    std::uint32_t counter = 0;
    std::string password;
    std::string checkId;
};

std::optional<ConsoleMailConfig> LoadConsoleMailConfig() {
    std::string data;
    if (!ReadFile(cf::sdk::UserDirPath() / "console" / "nwc24msg.cfg", &data)) {
        return std::nullopt;
    }
    const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
    auto be32 = [&](std::size_t at) {
        return (std::uint32_t(p[at]) << 24) | (std::uint32_t(p[at + 1]) << 16) | (std::uint32_t(p[at + 2]) << 8) |
               p[at + 3];
    };
    if (data.size() != 0x400 || be32(0) != 0x57634366 || be32(4) != 8) {
        WARN("console/nwc24msg.cfg is not a WiiConnect24 configuration; ignored");
        return std::nullopt;
    }
    std::uint32_t sum = 0;
    for (std::size_t at = 0; at < 0x3FC; at += 4) {
        sum += be32(at);
    }
    if (sum != be32(0x3FC)) {
        WARN("console/nwc24msg.cfg's checksum is wrong; ignored");
        return std::nullopt;
    }
    ConsoleMailConfig config;
    config.wiiNumber = (static_cast<std::uint64_t>(be32(8)) << 32) | be32(12);
    // The generation counter in the number (the file counts generations: one more)
    config.counter = static_cast<std::uint32_t>(UnscrambleWiiNumber(config.wiiNumber) >> 10) & 31;
    if (!CheckWiiNumber(config.wiiNumber) || be32(0x10) > 32) {
        WARN("console/nwc24msg.cfg has no valid Wii number; ignored");
        return std::nullopt;
    }
    auto text = [&](std::size_t at, std::size_t size) {
        std::size_t n = 0;
        while (n < size && p[at + n] != 0) {
            n++;
        }
        return std::string(reinterpret_cast<const char*>(p + at), n);
    };
    if (be32(0x14) == 2) {
        config.password = text(0x58, 0x20);
        config.checkId = text(0x78, 0x24);
    }
    return config;
}

// The console's own settings over the identity: its Wii number (and counter) and mail account, when its
// console is this identity's (the keys' NG ID is in the Wii number). True if something changed.
bool ApplyConsoleMailConfig(std::uint32_t keysConsoleId) {
    const std::optional<ConsoleMailConfig> config = LoadConsoleMailConfig();
    if (!config) {
        return false;
    }
    const std::uint32_t consoleId = static_cast<std::uint32_t>(UnscrambleWiiNumber(config->wiiNumber) >> 15);
    if (keysConsoleId == 0 || consoleId != keysConsoleId) {
        WARN("console/nwc24msg.cfg is console %08X's, not the console keys'; ignored", consoleId);
        return false;
    }
    if (sIdentity.consoleId == consoleId && sIdentity.wiiNumber == config->wiiNumber &&
        sIdentity.mailPassword == config->password && sIdentity.mailCheckId == config->checkId) {
        return false;
    }
    sIdentity.consoleId = consoleId;
    sIdentity.counter = config->counter;
    sIdentity.wiiNumber = config->wiiNumber;
    sIdentity.mailPassword = config->password;
    sIdentity.mailCheckId = config->checkId;
    LOG("imported console/nwc24msg.cfg: Wii number %016llu%s", static_cast<unsigned long long>(config->wiiNumber),
        config->password.empty() ? "" : " with its mail account");
    return true;
}

// This console's identity: loaded, or made on first use (the console's own nwc24msg.cfg over it)
Identity& GetIdentity() {
    if (sIdentityLoaded) {
        return sIdentity;
    }
    sIdentityLoaded = true;
    std::random_device rd;
    std::mt19937_64 rng64((static_cast<std::uint64_t>(rd()) << 32) | rd());
    const json j = ReadJson(Wc24Dir() / "identity.json", json::object());
    const std::uint32_t keysConsoleId = KeysConsoleId();
    if (j.contains("consoleId") && j.contains("wiiNumber")) {
        sIdentity.consoleId = j.value("consoleId", 0u);
        sIdentity.counter = j.value("counter", 0u);
        sIdentity.wiiNumber = std::strtoull(j.value("wiiNumber", std::string("0")).c_str(), nullptr, 10);
        sIdentity.nextMessageId = j.value("nextMessageId", 1u);
        sIdentity.dwcUserId = j.value("dwcUserId", std::uint64_t(0));
        sIdentity.mailPassword = j.value("mailPassword", std::string());
        sIdentity.mailCheckId = j.value("mailCheckId", std::string());
        if (CheckWiiNumber(sIdentity.wiiNumber)) {
            bool changed = false;
            if (sIdentity.dwcUserId == 0) { // made before the DWC user ID was kept
                sIdentity.dwcUserId = MakeDwcUserId(rng64);
                changed = true;
            }
            // Not the console the keys say (keys added later, or another console's), or made before the
            // port's range: remade, unless the Wii number is registered for mail (then it must stay; the player
            // can import the console's own nwc24msg.cfg instead)
            const bool wrong = keysConsoleId != 0 ? sIdentity.consoleId != keysConsoleId
                                                  : sIdentity.consoleId < kPortConsoleIdBase;
            if (wrong) {
                if (j.contains("mailPassword")) {
                    WARN("identity.json's console (%08X) is not the console keys'; kept: its Wii number has mail",
                         sIdentity.consoleId);
                } else {
                    MakeConsoleIdentity(keysConsoleId);
                    changed = true;
                }
            }
            if (ApplyConsoleMailConfig(keysConsoleId)) {
                changed = true;
            }
            if (changed) {
                SaveIdentity();
            }
            return sIdentity;
        }
        WARN("identity.json has an invalid Wii number; making a new one");
    }
    MakeConsoleIdentity(keysConsoleId);
    ApplyConsoleMailConfig(keysConsoleId);
    sIdentity.dwcUserId = MakeDwcUserId(rng64);
    sIdentity.nextMessageId = 1;
    SaveIdentity();
    return sIdentity;
}

std::uint32_t NextMessageId() {
    Identity& id = GetIdentity();
    const std::uint32_t msgId = id.nextMessageId;
    id.nextMessageId = msgId >= 0xFFFFF ? 1 : msgId + 1; // 5 hex digits in the Message-Id
    SaveIdentity();
    return msgId;
}

} // namespace

namespace cf::sdk::wc24 {

std::uint64_t ConsoleWiiNumber() {
    std::lock_guard lock(sMutex);
    return GetIdentity().wiiNumber;
}

std::uint64_t ConsoleDwcUserId() {
    std::lock_guard lock(sMutex);
    return GetIdentity().dwcUserId;
}

} // namespace cf::sdk::wc24

namespace {

// ---------------------------------------------------------------------------------------------
// Transport (the KD's part)
// ---------------------------------------------------------------------------------------------
std::string Digits(std::uint64_t id) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llu", static_cast<unsigned long long>(id));
    return buf;
}

fs::path LoopbackDir() {
    const char* dir = std::getenv("CF_WC24_LOOPBACK");
    return dir != nullptr && dir[0] != '\0' ? fs::u8path(dir) : fs::path();
}

std::string NameOf(const fs::path& path) {
    const auto name = path.filename().u8string();
    return std::string(name.begin(), name.end());
}

// The number in a mailbox file's name (s<n>.msg, r<n>.msg)
std::uint32_t MessageNumberOf(const fs::path& path) {
    const auto stem = path.stem().u8string();
    return static_cast<std::uint32_t>(std::strtoul(std::string(stem.begin(), stem.end()).c_str() + 1, nullptr, 10));
}

std::vector<fs::path> ListFiles(const fs::path& dir, char prefix) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = NameOf(e.path());
        if (e.is_regular_file(ec) && !name.empty() && name[0] == prefix && e.path().extension() == ".msg") {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Loopback: each outbox message to <loopback>/<recipient>/
void DeliverOutbox() {
    const fs::path loop = LoopbackDir();
    if (loop.empty()) {
        return;
    }
    std::error_code ec;
    for (const fs::path& file : ListFiles(Wc24Dir() / "outbox", 's')) {
        std::string text;
        if (!ReadFile(file, &text)) {
            continue;
        }
        bool delivered = true;
        // The recipients are the envelope's RCPT TO lines
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line) && line.rfind("DATA", 0) != 0) {
            if (line.rfind("RCPT TO:", 0) == 0) {
                const std::uint64_t to = ParseWiiAddress(line.substr(8));
                if (to == 0) {
                    continue;
                }
                const fs::path dir = loop / Digits(to);
                fs::create_directories(dir, ec);
                char name[64];
                std::snprintf(name, sizeof(name), "%016llu-%llu-%s", static_cast<unsigned long long>(GetIdentity().wiiNumber),
                              static_cast<unsigned long long>(std::time(nullptr)), NameOf(file).c_str());
                delivered &= WriteFile(dir / fs::u8path(name), text);
            }
        }
        if (delivered) {
            fs::remove(file, ec);
            LOG("sent %s (loopback)", NameOf(file).c_str());
        }
    }
}

std::uint32_t NextInboxId() {
    std::uint32_t next = 1;
    for (const fs::path& file : ListFiles(Wc24Dir() / "inbox", 'r')) {
        next = std::max<std::uint32_t>(next, MessageNumberOf(file) + 1);
    }
    return next;
}

// Loopback: this console's messages from <loopback>/<my number>/ into the inbox
void CollectInbox() {
    const fs::path loop = LoopbackDir();
    if (loop.empty()) {
        return;
    }
    const fs::path mine = loop / Digits(GetIdentity().wiiNumber);
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(mine, ec)) {
        if (e.is_regular_file(ec)) {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        std::string text;
        if (!ReadFile(file, &text)) {
            continue;
        }
        char name[32];
        std::snprintf(name, sizeof(name), "r%08u.msg", NextInboxId());
        if (WriteFile(Wc24Dir() / "inbox" / name, text)) {
            fs::remove(file, ec);
            LOG("received %s (loopback)", NameOf(file).c_str());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Message objects: the game's NWC24MsgObj (256 bytes it owns) keys its state here
// ---------------------------------------------------------------------------------------------
struct MsgState {
    bool outgoing = true;
    NWC24MsgType type = NWC24_MSGTYPE_RVL_HIDDEN;
    OutgoingMessage out;
    // Received
    std::uint32_t inboxId = 0;
    IncomingMessage in;
};

std::unordered_map<const NWC24MsgObj*, MsgState> sMessages;

MsgState* Lookup(const NWC24MsgObj* msg) {
    const auto it = sMessages.find(msg);
    return it != sMessages.end() ? &it->second : nullptr;
}

std::uint32_t FlagsOf(NWC24MsgType type) {
    switch (type) {
    case NWC24_MSGTYPE_RVL_MENU_SHARED:
        return MSG_FOR_RECIPIENT | MSG_FOR_APP | MSG_FOR_MENU;
    case NWC24_MSGTYPE_WII_APP:
        return MSG_FOR_RECIPIENT | MSG_FOR_APP;
    case NWC24_MSGTYPE_RVL_MENU:
        return MSG_FOR_RECIPIENT | MSG_FOR_MENU;
    case NWC24_MSGTYPE_RVL_HIDDEN:
        return MSG_FOR_RECIPIENT;
    case NWC24_MSGTYPE_PUBLIC:
        return MSG_FOR_PUBLIC;
    default:
        return 0;
    }
}

NWC24MsgType TypeOf(std::uint32_t flags) {
    if (flags & MSG_FOR_RECIPIENT) {
        if (flags & MSG_FOR_APP) {
            return (flags & MSG_FOR_MENU) ? NWC24_MSGTYPE_RVL_MENU_SHARED : NWC24_MSGTYPE_WII_APP;
        }
        return (flags & MSG_FOR_MENU) ? NWC24_MSGTYPE_RVL_MENU : NWC24_MSGTYPE_RVL_HIDDEN;
    }
    return NWC24_MSGTYPE_PUBLIC;
}

// The receive box search (NWC24InitSearchConds .. NWC24SearchMsgs): what this pass returned
std::vector<std::uint32_t> sSearchReturned;

// ---------------------------------------------------------------------------------------------
// Secret friends and download tasks
// ---------------------------------------------------------------------------------------------
json LoadSecretFriends() {
    return ReadJson(Wc24Dir() / "secret_friends.json", json::array());
}

// The download task in the game's NWC24DlTask (512 bytes, ours to lay out)
struct DlTask {
    std::uint32_t magic;
    std::uint32_t type;
    std::uint32_t appId;
    std::uint32_t flags;
    std::uint32_t userParam;
    std::uint32_t lastError;
    std::int32_t errorCount;
    std::int16_t count;
    std::uint16_t interval;
    std::uint8_t priority;
    std::uint8_t registered;
    char url[236];
    char fileName[64];
};
static_assert(sizeof(DlTask) <= sizeof(NWC24DlTask), "the task must fit the game's NWC24DlTask");
constexpr std::uint32_t kDlTaskMagic = 0x44545343; // "DTSC"

DlTask* TaskOf(NWC24DlTask* task) {
    return reinterpret_cast<DlTask*>(task);
}

const DlTask* TaskOf(const NWC24DlTask* task) {
    return reinterpret_cast<const DlTask*>(task);
}

json TaskToJson(const DlTask& t) {
    return {{"type", t.type},       {"appId", t.appId},         {"flags", t.flags},       {"userParam", t.userParam},
            {"lastError", t.lastError}, {"errorCount", t.errorCount}, {"count", t.count}, {"interval", t.interval},
            {"priority", t.priority}, {"url", t.url},          {"fileName", t.fileName}};
}

void TaskFromJson(const json& j, DlTask* t) {
    std::memset(t, 0, sizeof(*t));
    t->magic = kDlTaskMagic;
    t->type = j.value("type", 0u);
    t->appId = j.value("appId", 0u);
    t->flags = j.value("flags", 0u);
    t->userParam = j.value("userParam", 0u);
    t->lastError = j.value("lastError", 0u);
    t->errorCount = j.value("errorCount", 0);
    t->count = static_cast<std::int16_t>(j.value("count", 0));
    t->interval = static_cast<std::uint16_t>(j.value("interval", 0));
    t->priority = static_cast<std::uint8_t>(j.value("priority", 0));
    t->registered = 1;
    std::strncpy(t->url, j.value("url", std::string()).c_str(), sizeof(t->url) - 1);
    std::strncpy(t->fileName, j.value("fileName", std::string()).c_str(), sizeof(t->fileName) - 1);
}

json LoadTasks() {
    return ReadJson(Wc24Dir() / "dl_tasks.json", json::array());
}

std::string HomePath(const char* name) {
    return cf::sdk::NandHomeDir() + "/" + name;
}

// ---------------------------------------------------------------------------------------------
// Downloads (the KD's part): a task's file is fetched from the online service, its WiiConnect24
// header removed and its payload decrypted (Nintendo's format: docs/wc24/kd_protocol.md 6.3), and
// written into the download box, where the game reads it. Due tasks run on a worker thread.
// ---------------------------------------------------------------------------------------------
constexpr std::uint32_t kDlFlagRaw = 1 << 2;       // no WiiConnect24 header
constexpr std::uint32_t kDlFlagEncrypted = 1 << 3; // AES-128-OFB with the title's key
constexpr std::size_t kWc24HeaderSize = 0x140;     // magic, version, crypt type, IV at 0x30, RSA signature
constexpr std::int64_t kRetrySeconds = 60 * 60;

std::mutex sKdMutex; // one download pass at a time
std::atomic<bool> sKdRunning{false};
std::mutex sKdLogMutex;
std::string sKdLog;

void KdLog(const std::string& line) {
    LOG("%s", line.c_str());
    std::lock_guard lock(sKdLogMutex);
    sKdLog += line + "\n";
    if (sKdLog.size() > 4096) {
        sKdLog.erase(0, sKdLog.size() - 4096);
    }
}

// The file of a task's URL from the service, else (when the service lacks it) from Nintendo's own servers
cf::sdk::net::HttpResponse Fetch(const std::string& url) {
    const cf::online::ServiceInfo& service = cf::online::Info(cf::online::Current());
    const std::size_t hostStart = url.find("://");
    const std::string host =
        hostStart == std::string::npos ? std::string() : url.substr(hostStart + 3, url.find_first_of(":/", hostStart + 3) - hostStart - 3);
    cf::sdk::net::HttpResponse response;
    if (service.dnsServer != nullptr && service.dnsServer[0] != '\0') {
        const std::string address = cf::sdk::net::ResolveWith(service.dnsServer, host);
        if (!address.empty()) {
            response = cf::sdk::net::HttpRequest(url, address);
            KdLog(std::string(service.name) + ": HTTP " + std::to_string(response.status) + " " + url);
            if (response.status == 200 || !service.nintendoFallback) {
                return response;
            }
        } else {
            KdLog(std::string(service.name) + ": no address for " + host);
        }
    }
    if (service.nintendoFallback) {
        response = cf::sdk::net::HttpRequest(url, "");
        KdLog("Nintendo: HTTP " + std::to_string(response.status) + " " + url);
    }
    return response;
}

// A task's file, ready for the box; empty with the reason when it cannot be had
std::optional<std::vector<std::uint8_t>> DownloadFile(const json& task, std::string* error) {
    const std::string url = task.value("url", std::string());
    const std::uint32_t flags = task.value("flags", 0u);
    const cf::sdk::net::HttpResponse response = Fetch(url);
    if (response.status != 200) {
        *error = response.status == 0 ? response.error : "HTTP " + std::to_string(response.status);
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(response.body.begin(), response.body.end());
    if (flags & kDlFlagRaw) {
        return data;
    }
    if (data.size() < kWc24HeaderSize || std::memcmp(data.data(), "WC24", 4) != 0) {
        *error = "not a WiiConnect24 file";
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload(data.begin() + kWc24HeaderSize, data.end());
    if (flags & kDlFlagEncrypted) {
        // The key is the title's (NWC24SetDlKeys: wc24pubk.mod at 0x200); the IV is the file's
        std::string keys;
        if (!ReadFile(cf::sdk::NandHostPath(HomePath("wc24pubk.mod").c_str()), &keys) || keys.size() < 0x210) {
            *error = "no download keys";
            return std::nullopt;
        }
        cf::sdk::AesOfb(reinterpret_cast<const std::uint8_t*>(keys.data()) + 0x200, data.data() + 0x30,
                        payload.data(), payload.size());
    }
    return payload;
}

// Runs the due tasks (all with force), each once
void RunDownloads(bool force) {
    std::lock_guard kdLock(sKdMutex);
    json tasks;
    {
        std::lock_guard lock(sMutex);
        tasks = LoadTasks();
    }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    for (const json& task : tasks) {
        const std::string fileName = task.value("fileName", std::string());
        if (fileName.empty() || task.value("count", 0) <= 0 ||
            (!force && task.value("nextDownload", std::int64_t(0)) > now)) {
            continue;
        }
        const fs::path box = cf::sdk::NandHostPath(HomePath("wc24dl.vff").c_str());
        std::string error;
        std::optional<std::vector<std::uint8_t>> file;
        std::error_code ec;
        if (!fs::is_directory(box, ec)) {
            error = "no download box";
        } else {
            file = DownloadFile(task, &error);
        }
        if (file && !WriteFile(box / fs::u8path(fileName), std::string(file->begin(), file->end()))) {
            error = "cannot write " + fileName;
            file.reset();
        }
        KdLog(file ? "task " + std::to_string(task.value("userParam", 0u)) + ": " + fileName + " (" +
                         std::to_string(file->size()) + " bytes)"
                   : "task " + std::to_string(task.value("userParam", 0u)) + " failed: " + error);

        // The task's state, in the current list (the game may have changed it meanwhile)
        std::lock_guard lock(sMutex);
        json current = LoadTasks();
        for (json& j : current) {
            if (j.value("appId", 0u) != task.value("appId", 0u) ||
                j.value("userParam", 0u) != task.value("userParam", 0u)) {
                continue;
            }
            if (file) {
                j["lastError"] = 0;
                j["errorCount"] = 0;
                j["count"] = j.value("count", 1) - 1;
                j["nextDownload"] = now + static_cast<std::int64_t>(j.value("interval", 0)) * 60;
            } else {
                j["lastError"] = 1;
                j["errorCount"] = j.value("errorCount", 0) + 1;
                j["nextDownload"] = now + kRetrySeconds;
            }
        }
        WriteJson(Wc24Dir() / "dl_tasks.json", current);
    }
}

// A download pass in the background, unless one is running
void StartDownloads(bool force) {
    if (sKdRunning.exchange(true)) {
        return;
    }
    std::thread([force] {
        RunDownloads(force);
        sKdRunning = false;
    }).detach();
}


// ---------------------------------------------------------------------------------------------
// Mail (the KD's part): the console's account on the service's mail server (registered on first
// use), the outbox sent, and waiting mail received into the inbox, on a worker thread woken by commits,
// by the game's receive-box search and by the debug UI, else every few minutes while the game runs.
// Received mail from senders who are not the application's friends (secret friends) or this console is
// dropped: the server does not check (docs/wc24/README.md).
// ---------------------------------------------------------------------------------------------
constexpr std::uint32_t kSelfTestAppId = 0x53554954; // 'SUIT': the debug self-test's messages
constexpr auto kMailInterval = std::chrono::minutes(10);
constexpr int kMaxRegistrationAttempts = 8;
constexpr std::size_t kMaxSendBatch = 16; // send.cgi's limit

std::mutex sMailLogMutex;
std::string sMailLog;
std::mutex sMailWakeMutex;
std::condition_variable sMailWake;
bool sMailWakeRequested = false;
std::atomic<bool> sMailThreadStarted{false};
std::vector<std::uint8_t> sSelfTestPayload; // the self-test message's attachment, until it comes back (sMutex)

void LogMail(const std::string& line) {
    LOG("%s", line.c_str());
    std::lock_guard lock(sMailLogMutex);
    sMailLog += line + "\n";
    if (sMailLog.size() > 6144) {
        sMailLog.erase(0, sMailLog.size() - 6144);
    }
}

bool LoopbackMode() {
    return !LoopbackDir().empty();
}

std::string MailUrl() {
    const cf::online::ServiceInfo& service = cf::online::Info(cf::online::Current());
    return service.mailUrl != nullptr ? service.mailUrl : "";
}

std::string MailDomain() {
    const cf::online::ServiceInfo& service = cf::online::Info(cf::online::Current());
    return service.mailDomain != nullptr ? service.mailDomain : "wii.com";
}

// The console's mail account, registered first if it has none (sMutex not held: network)
std::optional<MailAccount> GetMailAccount(const std::string& url) {
    for (int attempt = 0; attempt < kMaxRegistrationAttempts; attempt++) {
        std::uint64_t wiiNumber;
        {
            std::lock_guard lock(sMutex);
            Identity& id = GetIdentity();
            if (!id.mailPassword.empty() && !id.mailCheckId.empty()) {
                return MailAccount{id.wiiNumber, id.mailPassword, id.mailCheckId};
            }
            wiiNumber = id.wiiNumber;
        }
        const CgiReply reply = RegisterAccount(url, wiiNumber);
        std::lock_guard lock(sMutex);
        Identity& id = GetIdentity();
        if (reply.code == kCgiSuccess && reply.values.count("passwd") != 0 && reply.values.count("mlchkid") != 0) {
            id.mailPassword = reply.values.at("passwd");
            id.mailCheckId = reply.values.at("mlchkid");
            SaveIdentity();
            LogMail("registered " + Digits(id.wiiNumber) + " for mail");
            continue;
        }
        if (reply.code != kCgiDuplicateRegistration) {
            LogMail("registration failed: " + (reply.code == 0 ? reply.error : std::to_string(reply.code) + " " + reply.message));
            return std::nullopt;
        }
        // The Wii number is someone else's. The console's own (from its keys) stays: its account is the
        // console's, whose nwc24msg.cfg has the credentials. A port console moves on to its next number, as
        // the KD does
        if (id.consoleId < kPortConsoleIdBase) {
            LogMail("registration failed: " + Digits(id.wiiNumber) +
                    " already has a mail account: the console's own. Put the console's /shared2/wc24/nwc24msg.cfg in the "
                    "user directory's console folder and restart");
            return std::nullopt;
        }
        do {
            id.counter = (id.counter + 1) & 31;
            id.wiiNumber = GenerateWiiNumber(id.consoleId, id.counter, MODEL_RVL, AreaOfGame());
        } while (!CheckWiiNumber(id.wiiNumber));
        SaveIdentity();
        LogMail("Wii number taken: now " + Digits(id.wiiNumber));
    }
    LogMail("registration failed: no free Wii number");
    return std::nullopt;
}

bool IsSecretFriend(std::uint64_t wiiNumber) {
    const std::string id = Digits(wiiNumber);
    for (const json& f : LoadSecretFriends()) {
        if (f.value("id", std::string()) == id) {
            return true;
        }
    }
    return false;
}

// The outbox, in batches
void SendOutbox(const std::string& url, const MailAccount& account) {
    std::vector<fs::path> files;
    {
        std::lock_guard lock(sMutex);
        files = ListFiles(Wc24Dir() / "outbox", 's');
    }
    for (std::size_t start = 0; start < files.size(); start += kMaxSendBatch) {
        std::vector<fs::path> batch(files.begin() + start, files.begin() + std::min(files.size(), start + kMaxSendBatch));
        std::vector<std::string> messages;
        std::vector<fs::path> sent;
        for (const fs::path& file : batch) {
            std::string text;
            if (ReadFile(file, &text)) {
                messages.push_back(std::move(text));
                sent.push_back(file);
            }
        }
        if (messages.empty()) {
            continue;
        }
        std::vector<int> codes;
        const CgiReply reply = SendMail(url, account, messages, &codes);
        if (reply.code != kCgiSuccess) {
            LogMail("send failed: " + (reply.code == 0 ? reply.error : std::to_string(reply.code) + " " + reply.message));
            return; // kept for the next pass
        }
        std::lock_guard lock(sMutex);
        for (std::size_t i = 0; i < sent.size(); i++) {
            std::error_code ec;
            if (codes[i] == kCgiSuccess) {
                fs::remove(sent[i], ec);
                LogMail("sent " + NameOf(sent[i]));
            } else {
                LogMail("not sent " + NameOf(sent[i]) + ": " + std::to_string(codes[i]));
                if (codes[i] >= 300) { // refused (not a passing failure): it would be refused again
                    fs::remove(sent[i], ec);
                }
            }
        }
    }
}

// Waiting mail into the inbox (from the application's friends and this console only)
void ReceiveWaiting(const std::string& url, const MailAccount& account) {
    bool hasMail = false;
    const CgiReply check = CheckMail(url, account, &hasMail);
    if (check.code != kCgiSuccess) {
        LogMail("check failed: " + (check.code == 0 ? check.error : std::to_string(check.code) + " " + check.message));
        return;
    }
    if (!hasMail) {
        return;
    }
    std::vector<std::string> messages;
    const CgiReply reply = ReceiveMail(url, account, &messages);
    if (reply.code != kCgiSuccess) {
        LogMail("receive failed: " + (reply.code == 0 ? reply.error : std::to_string(reply.code) + " " + reply.message));
        return;
    }
    {
        std::lock_guard lock(sMutex);
        for (const std::string& text : messages) {
            const std::optional<IncomingMessage> message = ParseMessage(text);
            if (!message) {
                LogMail("received a message that does not parse; dropped");
                continue;
            }
            if (message->from == account.wiiNumber && message->appId == kSelfTestAppId) {
                const bool same = !message->attachments.empty() && message->attachments[0].data == sSelfTestPayload;
                LogMail(same ? "self-test: the message came back intact (" + std::to_string(sSelfTestPayload.size()) +
                                   "-byte attachment matches)"
                             : "self-test: a test message came back, but its attachment differs");
                sSelfTestPayload.clear();
                continue;
            }
            if (message->from != account.wiiNumber && !IsSecretFriend(message->from)) {
                LogMail("dropped a message from " + Digits(message->from) + " (not a friend)");
                continue;
            }
            char name[32];
            std::snprintf(name, sizeof(name), "r%08u.msg", NextInboxId());
            if (WriteFile(Wc24Dir() / "inbox" / name, text)) {
                LogMail("received " + std::string(name) + " from " + Digits(message->from));
            }
        }
    }
    if (!messages.empty()) {
        const CgiReply deleted = DeleteMail(url, account, static_cast<int>(messages.size()));
        if (deleted.code != kCgiSuccess) {
            LogMail("delete failed: " + (deleted.code == 0 ? deleted.error : std::to_string(deleted.code)));
        }
    }
}

// One pass: send, then receive
void RunMail() {
    if (LoopbackMode()) {
        std::lock_guard lock(sMutex);
        DeliverOutbox();
        CollectInbox();
        return;
    }
    const std::string url = MailUrl();
    if (url.empty()) {
        return;
    }
    const std::optional<MailAccount> account = GetMailAccount(url);
    if (!account) {
        return;
    }
    SendOutbox(url, *account);
    ReceiveWaiting(url, *account);
}

void WakeMail() {
    {
        std::lock_guard lock(sMailWakeMutex);
        sMailWakeRequested = true;
    }
    sMailWake.notify_one();
}

// The mail worker: a pass now, then when woken or every kMailInterval
void StartMail() {
    if (sMailThreadStarted.exchange(true)) {
        WakeMail();
        return;
    }
    std::thread([] {
        for (;;) {
            RunMail();
            std::unique_lock lock(sMailWakeMutex);
            sMailWake.wait_for(lock, kMailInterval, [] { return sMailWakeRequested; });
            sMailWakeRequested = false;
        }
    }).detach();
}

} // namespace

void cf::wc24::DownloadNow() {
    StartDownloads(true);
}

std::string cf::wc24::DownloadLog() {
    std::lock_guard lock(sKdLogMutex);
    return sKdLog;
}

unsigned long long cf::wc24::WiiNumber() {
    std::lock_guard lock(sMutex);
    return GetIdentity().wiiNumber;
}

void cf::wc24::MailNow() {
    StartMail();
}

std::string cf::wc24::MailLog() {
    std::lock_guard lock(sMailLogMutex);
    return sMailLog;
}

std::string cf::wc24::MailStatus() {
    std::lock_guard lock(sMutex);
    const Identity& id = GetIdentity();
    std::string status = "Wii number " + Digits(id.wiiNumber).insert(12, "-").insert(8, "-").insert(4, "-");
    if (LoopbackMode()) {
        return status + " (loopback)";
    }
    return status + (id.mailPassword.empty() ? " (not registered for mail yet)" : " (registered for mail)");
}

// A message from this console to itself through the mail server, with an attachment checked when it
// comes back (and kept from the game: its application ID is the self-test's)
void cf::wc24::MailSelfTest() {
    {
        std::lock_guard lock(sMutex);
        OutgoingMessage out;
        out.flags = MSG_FOR_RECIPIENT | MSG_FOR_APP;
        out.from = GetIdentity().wiiNumber;
        out.to = {out.from};
        out.appId = kSelfTestAppId;
        out.groupId = GroupId();
        out.domain = MailDomain();
        out.charset = NWC24_US_ASCII;
        out.encoding = NWC24_ENC_7BIT;
        const std::string text = "suitcase WiiConnect24 mail self-test";
        out.text.assign(text.begin(), text.end());
        std::random_device random;
        sSelfTestPayload.resize(256);
        for (std::uint8_t& b : sSelfTestPayload) {
            b = static_cast<std::uint8_t>(random());
        }
        out.attachments.push_back({static_cast<std::uint32_t>(NWC24_APPLICATION_OCTET_STREAM), sSelfTestPayload});
        out.id = NextMessageId();
        char name[32];
        std::snprintf(name, sizeof(name), "s%08u.msg", out.id);
        if (!WriteFile(Wc24Dir() / "outbox" / name, FormatSendMessage(out))) {
            LogMail("self-test: cannot write the outbox");
            return;
        }
        LogMail("self-test: queued " + std::string(name) + " to this console");
    }
    StartMail();
}

extern "C" {

// ---------------------------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------------------------
NWC24Err NWC24OpenLib(void* work) {
    (void)work;
    std::lock_guard lock(sMutex);
    GetIdentity();
    sOpened = true;
    StartDownloads(false); // the KD's scheduler: tasks that came due while the game was off
    StartMail();
    return NWC24_OK;
}

NWC24Err NWC24CloseLib() {
    std::lock_guard lock(sMutex);
    if (!sOpened) {
        return NWC24_ERR_LIB_NOT_OPENED;
    }
    sOpened = false;
    return NWC24_OK;
}

// WiiConnect24 is on (the console setting and the game's own both)
NWC24Err NWC24Check(u32 usage) {
    (void)usage;
    std::lock_guard lock(sMutex);
    return sOpened ? NWC24_OK : NWC24_ERR_LIB_NOT_OPENED;
}

s32 NWC24GetErrorCode() {
    return 0;
}

// The KD's scheduler: nothing runs behind the game's back here, so suspending always succeeds
s32 NWC24SuspendScheduler() {
    return 0;
}

s32 NWC24TrySuspendScheduler() {
    return 0;
}

s32 NWC24ResumeScheduler() {
    return 0;
}

NWC24Err NWC24GetMyUserId(NWC24UserId* pId) {
    std::lock_guard lock(sMutex);
    *pId = GetIdentity().wiiNumber;
    return NWC24_OK;
}

// As the SDK: this console's own number is NWC24_ERR_ID_GENERATED, a malformed one NWC24_ERR_ID_CRC
NWC24Err NWC24CheckUserId(NWC24UserId id) {
    std::lock_guard lock(sMutex);
    if (id == GetIdentity().wiiNumber) {
        return NWC24_ERR_ID_GENERATED;
    }
    return CheckWiiNumber(id) ? NWC24_OK : NWC24_ERR_ID_CRC;
}

// ---------------------------------------------------------------------------------------------
// The Wii Address Book (the Message Board's friends): empty
// ---------------------------------------------------------------------------------------------
NWC24Err NWC24GetNumFriendInfos(u32* numFriendInfo) {
    *numFriendInfo = 0;
    return NWC24_OK;
}

BOOL NWC24IsFriendInfoThere(u32 index) {
    (void)index;
    return FALSE;
}

NWC24Err NWC24ReadFriendInfo(NWC24FriendInfo* info, u32 index) {
    (void)info;
    (void)index;
    return NWC24_ERR_NOT_FOUND;
}

NWC24Err NWC24CheckFriendInfo(const NWC24FriendInfo* friendInfo) {
    (void)friendInfo;
    return NWC24_ERR_NOT_FOUND;
}

// ---------------------------------------------------------------------------------------------
// Secret friends: the application's own friend records (the game's Wi-Fi friends)
// ---------------------------------------------------------------------------------------------
NWC24Err NWC24AddSecretFriendInfo(const NWC24SecretFriendInfo* info) {
    std::lock_guard lock(sMutex);
    json friends = LoadSecretFriends();
    const std::string id = Digits(info->id);
    json entry = {{"id", id}, {"key", info->key}, {"appId", info->appId}, {"time", info->time}};
    for (json& f : friends) {
        if (f.value("id", std::string()) == id && f.value("appId", 0u) == info->appId) {
            f = entry;
            return WriteJson(Wc24Dir() / "secret_friends.json", friends) ? NWC24_OK : NWC24_ERR_FILE_WRITE;
        }
    }
    if (friends.size() >= kMaxSecretFriends) {
        return NWC24_ERR_FULL;
    }
    friends.push_back(entry);
    return WriteJson(Wc24Dir() / "secret_friends.json", friends) ? NWC24_OK : NWC24_ERR_FILE_WRITE;
}

NWC24Err NWC24DeleteSecretFriendInfo(const NWC24SecretFriendInfo* info) {
    std::lock_guard lock(sMutex);
    json friends = LoadSecretFriends();
    const std::string id = Digits(info->id);
    const auto before = friends.size();
    friends.erase(std::remove_if(friends.begin(), friends.end(),
                                 [&](const json& f) {
                                     return f.value("id", std::string()) == id && f.value("appId", 0u) == info->appId;
                                 }),
                  friends.end());
    if (friends.size() == before) {
        return NWC24_ERR_NOT_FOUND;
    }
    return WriteJson(Wc24Dir() / "secret_friends.json", friends) ? NWC24_OK : NWC24_ERR_FILE_WRITE;
}

NWC24Err NWC24GetNumSecretFriendInfos(u32* num) {
    std::lock_guard lock(sMutex);
    *num = static_cast<u32>(LoadSecretFriends().size());
    return NWC24_OK;
}

NWC24Err NWC24ReadSecretFriendInfo(NWC24SecretFriendInfo* info, u32 index) {
    std::lock_guard lock(sMutex);
    const json friends = LoadSecretFriends();
    if (index >= friends.size()) {
        return NWC24_ERR_NOT_FOUND;
    }
    const json& f = friends[index];
    std::memset(info, 0, sizeof(*info));
    info->id = std::strtoull(f.value("id", std::string("0")).c_str(), nullptr, 10);
    info->key = f.value("key", 0);
    info->appId = f.value("appId", 0u);
    info->time = f.value("time", 0u);
    return NWC24_OK;
}

// ---------------------------------------------------------------------------------------------
// Messages: building and committing
// ---------------------------------------------------------------------------------------------
NWC24Err NWC24InitMsgObj(NWC24MsgObj* msg, NWC24MsgType type) {
    const std::uint32_t flags = FlagsOf(type);
    if (flags == 0) {
        return NWC24_ERR_INVALID_VALUE;
    }
    std::lock_guard lock(sMutex);
    std::memset(msg, 0, sizeof(*msg));
    MsgState& state = sMessages[msg];
    state = MsgState();
    state.type = type;
    state.out.flags = flags;
    state.out.from = GetIdentity().wiiNumber;
    state.out.appId = AppId();
    state.out.groupId = GroupId();
    state.out.charset = NWC24_US_ASCII;
    state.out.encoding = NWC24_ENC_7BIT;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgToId(NWC24MsgObj* msg, NWC24UserId id) {
    std::lock_guard lock(sMutex);
    MsgState* s = Lookup(msg);
    if (s == nullptr || !s->outgoing) {
        return NWC24_ERR_PROTECTED;
    }
    if (!(s->out.flags & MSG_FOR_RECIPIENT)) {
        return NWC24_ERR_NOT_SUPPORTED;
    }
    if (s->out.to.size() >= kMaxRecipients) {
        return NWC24_ERR_FULL;
    }
    s->out.to.push_back(id);
    return NWC24_OK;
}

// E-mail recipients: the game's Message Board path (accepted; dropped at commit)
NWC24Err NWC24SetMsgToStr(NWC24MsgObj* msg, const char* addr, u32 length) {
    (void)addr;
    (void)length;
    std::lock_guard lock(sMutex);
    MsgState* s = Lookup(msg);
    return s != nullptr && (s->out.flags & MSG_FOR_PUBLIC) ? NWC24_OK : NWC24_ERR_NOT_SUPPORTED;
}

NWC24Err NWC24SetMsgAttached(NWC24MsgObj* msg, const void* attachData, u32 attachSize, NWC24MIMEType type) {
    std::lock_guard lock(sMutex);
    MsgState* s = Lookup(msg);
    if (s == nullptr || !s->outgoing) {
        return NWC24_ERR_PROTECTED;
    }
    if (attachData == nullptr || attachSize == 0) {
        return NWC24_ERR_NULL;
    }
    if (s->out.attachments.size() >= kMaxAttachments) {
        return NWC24_ERR_FULL;
    }
    const u8* p = static_cast<const u8*>(attachData);
    s->out.attachments.push_back({static_cast<std::uint32_t>(type), std::vector<std::uint8_t>(p, p + attachSize)});
    return NWC24_OK;
}

NWC24Err NWC24SetMsgDwcSize(NWC24MsgObj* msg, u32 dwcSize) {
    std::lock_guard lock(sMutex);
    MsgState* s = Lookup(msg);
    if (s == nullptr || !s->outgoing) {
        return NWC24_ERR_PROTECTED;
    }
    s->out.dwcId = dwcSize;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgText(NWC24MsgObj* msg, const char* text, u32 textLen, NWC24Charset charset, NWC24Encoding encoding) {
    std::lock_guard lock(sMutex);
    MsgState* s = Lookup(msg);
    if (s == nullptr || !s->outgoing) {
        return NWC24_ERR_PROTECTED;
    }
    s->out.text.assign(text, text + textLen);
    s->out.charset = static_cast<std::uint32_t>(charset);
    s->out.encoding = static_cast<std::uint32_t>(encoding);
    return NWC24_OK;
}

// Message Board fields (the game's board messages, which are dropped)
NWC24Err NWC24SetMsgAltName(NWC24MsgObj* msg, const u16* altName, u32 altNameLen) {
    (void)msg;
    (void)altName;
    (void)altNameLen;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgMBNoReply(NWC24MsgObj* msg, BOOL mbNoReplyFlag) {
    (void)msg;
    (void)mbNoReplyFlag;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgLedPatternRaw(NWC24MsgObj* msg, u32 ledPattern) {
    (void)msg;
    (void)ledPattern;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgSubjectAndTextPublic(NWC24MsgObj* msg, const u16* subject, u32 subjectLen, const u16* text,
                                         u32 textLen, NWC24EncodingRegion encRegion, u16 unk, u8* work, u32 workSize) {
    (void)msg, (void)subject, (void)subjectLen, (void)text, (void)textLen, (void)encRegion, (void)unk, (void)work,
        (void)workSize;
    return NWC24_OK;
}

// Hidden application messages (letters) go to the outbox; the Message Board and e-mail are dropped
NWC24Err NWC24CommitMsg(NWC24MsgObj* msg) {
    std::lock_guard lock(sMutex);
    MsgState* s = Lookup(msg);
    if (s == nullptr || !s->outgoing) {
        return NWC24_ERR_PROTECTED;
    }
    if (s->type != NWC24_MSGTYPE_RVL_HIDDEN && s->type != NWC24_MSGTYPE_WII_APP) {
        LOG("a Message Board message was dropped (not supported)");
        return NWC24_OK;
    }
    if (s->out.to.empty()) {
        return NWC24_ERR_NULL;
    }
    s->out.id = NextMessageId();
    s->out.domain = MailDomain();
    char name[32];
    std::snprintf(name, sizeof(name), "s%08u.msg", s->out.id);
    if (!WriteFile(Wc24Dir() / "outbox" / name, FormatSendMessage(s->out))) {
        return NWC24_ERR_FILE_WRITE;
    }
    LOG("queued %s for %016llu", name, static_cast<unsigned long long>(s->out.to[0]));
    s->outgoing = false; // delivering: no more changes
    WakeMail();
    return NWC24_OK;
}

// ---------------------------------------------------------------------------------------------
// Messages: the receive box
// ---------------------------------------------------------------------------------------------
// The inbox as the mail worker has filled it (it is asked for more now, for the next search)
NWC24Err NWC24InitSearchConds() {
    std::lock_guard lock(sMutex);
    sSearchReturned.clear();
    WakeMail();
    return NWC24_OK;
}

NWC24Err NWC24SetSearchCondMsgBox(NWC24MBoxType msgBoxType) {
    return msgBoxType == NWC24_MBOX_TYPE_RECV ? NWC24_OK : NWC24_ERR_NOT_SUPPORTED;
}

NWC24Err NWC24SearchMsgs(NWC24MsgObj* msgs, u32 max, u32* numRead, u32* numRemaining) {
    std::lock_guard lock(sMutex);
    std::vector<fs::path> files;
    for (const fs::path& file : ListFiles(Wc24Dir() / "inbox", 'r')) {
        const std::uint32_t id = MessageNumberOf(file);
        if (std::find(sSearchReturned.begin(), sSearchReturned.end(), id) == sSearchReturned.end()) {
            files.push_back(file);
        }
    }
    u32 read = 0;
    std::size_t i = 0;
    for (; i < files.size() && read < max; i++) {
        const std::uint32_t id = MessageNumberOf(files[i]);
        sSearchReturned.push_back(id);
        std::string text;
        if (!ReadFile(files[i], &text)) {
            continue;
        }
        std::optional<IncomingMessage> parsed = ParseMessage(text);
        if (!parsed) {
            WARN("%s is not a message; skipped", NameOf(files[i]).c_str());
            continue;
        }
        NWC24MsgObj* obj = &msgs[read++];
        std::memset(obj, 0, sizeof(*obj));
        MsgState& state = sMessages[obj];
        state = MsgState();
        state.outgoing = false;
        state.inboxId = id;
        state.in = std::move(*parsed);
        state.type = TypeOf(state.in.flags);
    }
    if (numRead != nullptr) {
        *numRead = read;
    }
    if (numRemaining != nullptr) {
        *numRemaining = static_cast<u32>(files.size() - i);
    }
    return NWC24_OK;
}

NWC24Err NWC24DeleteMsg(NWC24MBoxType msgBoxType, u32 index) {
    if (msgBoxType != NWC24_MBOX_TYPE_RECV) {
        return NWC24_ERR_NOT_SUPPORTED;
    }
    std::lock_guard lock(sMutex);
    char name[32];
    std::snprintf(name, sizeof(name), "r%08u.msg", index);
    std::error_code ec;
    return fs::remove(Wc24Dir() / "inbox" / name, ec) ? NWC24_OK : NWC24_ERR_NOT_FOUND;
}

NWC24Err NWC24GetMsgId(const NWC24MsgObj* msg, u32* msgId) {
    std::lock_guard lock(sMutex);
    const MsgState* s = Lookup(msg);
    if (s == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *msgId = s->outgoing ? s->out.id : s->inboxId;
    return NWC24_OK;
}

NWC24Err NWC24GetMsgType(const NWC24MsgObj* msg, NWC24MsgType* type) {
    std::lock_guard lock(sMutex);
    const MsgState* s = Lookup(msg);
    if (s == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *type = s->type;
    return NWC24_OK;
}

NWC24Err NWC24GetMsgAppId(const NWC24MsgObj* msg, u32* appId) {
    std::lock_guard lock(sMutex);
    const MsgState* s = Lookup(msg);
    if (s == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *appId = s->outgoing ? s->out.appId : s->in.appId;
    return NWC24_OK;
}

NWC24Err NWC24GetMsgFromId(const NWC24MsgObj* msg, NWC24UserId* fromId) {
    std::lock_guard lock(sMutex);
    const MsgState* s = Lookup(msg);
    if (s == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *fromId = s->outgoing ? s->out.from : s->in.from;
    return NWC24_OK;
}

NWC24Err NWC24GetMsgNumAttached(const NWC24MsgObj* msg, u32* numAttach) {
    std::lock_guard lock(sMutex);
    const MsgState* s = Lookup(msg);
    if (s == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *numAttach = static_cast<u32>(s->outgoing ? s->out.attachments.size() : s->in.attachments.size());
    return NWC24_OK;
}

static const Attachment* AttachmentOf(const NWC24MsgObj* msg, u32 index) {
    const MsgState* s = Lookup(msg);
    if (s == nullptr) {
        return nullptr;
    }
    const std::vector<Attachment>& list = s->outgoing ? s->out.attachments : s->in.attachments;
    return index < list.size() ? &list[index] : nullptr;
}

NWC24Err NWC24GetMsgAttachedSize(const NWC24MsgObj* msg, u32 attachIndex, u32* attachSize) {
    std::lock_guard lock(sMutex);
    const Attachment* a = AttachmentOf(msg, attachIndex);
    if (a == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *attachSize = static_cast<u32>(a->data.size());
    return NWC24_OK;
}

NWC24Err NWC24GetMsgAttachedType(const NWC24MsgObj* msg, u32 attachIndex, NWC24MIMEType* attachType) {
    std::lock_guard lock(sMutex);
    const Attachment* a = AttachmentOf(msg, attachIndex);
    if (a == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *attachType = static_cast<NWC24MIMEType>(a->mimeType);
    return NWC24_OK;
}

NWC24Err NWC24ReadMsgAttached(const NWC24MsgObj* msg, u32 attachIndex, u8* attachData, u32 attachSize) {
    std::lock_guard lock(sMutex);
    const Attachment* a = AttachmentOf(msg, attachIndex);
    if (a == nullptr) {
        return NWC24_ERR_INVALID_VALUE;
    }
    if (a->data.size() > attachSize) {
        return NWC24_ERR_NOMEM;
    }
    std::memcpy(attachData, a->data.data(), a->data.size());
    return NWC24_OK;
}

// ---------------------------------------------------------------------------------------------
// Download tasks
// ---------------------------------------------------------------------------------------------
NWC24Err NWC24InitDlTask(NWC24DlTask* dlTask, NWC24DLType dlType) {
    DlTask* t = TaskOf(dlTask);
    std::memset(dlTask, 0, sizeof(*dlTask));
    t->magic = kDlTaskMagic;
    t->type = static_cast<std::uint32_t>(dlType);
    t->appId = AppId();
    return NWC24_OK;
}

#define TASK_SETTER(name, field, type)                                                                                 \
    NWC24Err name(NWC24DlTask* dlTask, type value) {                                                                   \
        DlTask* t = TaskOf(dlTask);                                                                                    \
        if (t->magic != kDlTaskMagic) {                                                                                \
            return NWC24_ERR_INVALID_VALUE;                                                                            \
        }                                                                                                              \
        t->field = value;                                                                                              \
        return NWC24_OK;                                                                                               \
    }
TASK_SETTER(NWC24SetDlPriority, priority, u8)
TASK_SETTER(NWC24SetDlInterval, interval, u16)
TASK_SETTER(NWC24SetDlFlags, flags, u32)
TASK_SETTER(NWC24SetDlCount, count, s16)
TASK_SETTER(NWC24SetDlUserParam, userParam, u32)
#undef TASK_SETTER

NWC24Err NWC24SetDlUrl(NWC24DlTask* dlTask, const char* dlUrl) {
    DlTask* t = TaskOf(dlTask);
    if (t->magic != kDlTaskMagic || dlUrl == nullptr || std::strlen(dlUrl) >= sizeof(t->url)) {
        return NWC24_ERR_INVALID_VALUE;
    }
    std::strcpy(t->url, dlUrl);
    return NWC24_OK;
}

NWC24Err NWC24SetDlFilename(NWC24DlTask* dlTask, const char* dlFileName) {
    DlTask* t = TaskOf(dlTask);
    if (t->magic != kDlTaskMagic || dlFileName == nullptr || std::strlen(dlFileName) >= sizeof(t->fileName)) {
        return NWC24_ERR_INVALID_VALUE;
    }
    std::strcpy(t->fileName, dlFileName);
    return NWC24_OK;
}

NWC24Err NWC24GetDlFilename(const NWC24DlTask* dlTask, char* dlFileName, u32 size) {
    const DlTask* t = TaskOf(dlTask);
    if (t->magic != kDlTaskMagic) {
        return NWC24_ERR_INVALID_VALUE;
    }
    const std::size_t len = std::strlen(t->fileName);
    if (len == 0) {
        return NWC24_ERR_NOT_FOUND;
    }
    if (len + 1 > size) {
        return NWC24_ERR_NOMEM;
    }
    std::memcpy(dlFileName, t->fileName, len + 1);
    return NWC24_OK;
}

// This title's registered task with the user parameter
NWC24Err NWC24GetDlTaskMine(NWC24DlTask* dlTask, u32 dlParam) {
    std::lock_guard lock(sMutex);
    for (const json& j : LoadTasks()) {
        if (j.value("appId", 0u) == AppId() && j.value("userParam", 0u) == dlParam) {
            TaskFromJson(j, TaskOf(dlTask));
            return NWC24_OK;
        }
    }
    return NWC24_ERR_NOT_FOUND;
}

static NWC24Err StoreTask(const DlTask& t, bool add) {
    json tasks = LoadTasks();
    for (json& j : tasks) {
        if (j.value("appId", 0u) == t.appId && j.value("userParam", 0u) == t.userParam) {
            json updated = TaskToJson(t);
            for (const char* key : {"nextDownload", "lastError", "errorCount"}) {
                if (j.contains(key)) {
                    updated[key] = j[key];
                }
            }
            j = std::move(updated);
            return WriteJson(Wc24Dir() / "dl_tasks.json", tasks) ? NWC24_OK : NWC24_ERR_FILE_WRITE;
        }
    }
    if (!add) {
        return NWC24_ERR_NOT_FOUND;
    }
    tasks.push_back(TaskToJson(t));
    return WriteJson(Wc24Dir() / "dl_tasks.json", tasks) ? NWC24_OK : NWC24_ERR_FILE_WRITE;
}

NWC24Err NWC24AddDlTask(NWC24DlTask* dlTask) {
    std::lock_guard lock(sMutex);
    DlTask* t = TaskOf(dlTask);
    if (t->magic != kDlTaskMagic) {
        return NWC24_ERR_INVALID_VALUE;
    }
    t->registered = 1;
    LOG("download task %u: %s -> %s", t->userParam, t->url, t->fileName);
    const NWC24Err result = StoreTask(*t, true);
    StartDownloads(false);
    return result;
}

NWC24Err NWC24UpdateDlTask(NWC24DlTask* dlTask) {
    std::lock_guard lock(sMutex);
    const DlTask* t = TaskOf(dlTask);
    if (t->magic != kDlTaskMagic) {
        return NWC24_ERR_INVALID_VALUE;
    }
    const NWC24Err result = StoreTask(*t, false);
    StartDownloads(false);
    return result;
}

NWC24Err NWC24DeleteDlTask(NWC24DlTask* dlTask) {
    std::lock_guard lock(sMutex);
    const DlTask* t = TaskOf(dlTask);
    json tasks = LoadTasks();
    const auto before = tasks.size();
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                               [&](const json& j) {
                                   return j.value("appId", 0u) == t->appId && j.value("userParam", 0u) == t->userParam;
                               }),
                tasks.end());
    if (tasks.size() == before) {
        return NWC24_ERR_NOT_FOUND;
    }
    return WriteJson(Wc24Dir() / "dl_tasks.json", tasks) ? NWC24_OK : NWC24_ERR_FILE_WRITE;
}

NWC24Err NWC24GetDlError(const NWC24DlTask* dlTask, u32* lastError, s32* errorCount) {
    const DlTask* t = TaskOf(dlTask);
    if (t->magic != kDlTaskMagic) {
        return NWC24_ERR_INVALID_VALUE;
    }
    *lastError = t->lastError;
    *errorCount = t->errorCount;
    return NWC24_OK;
}

NWC24Err NWC24GetDlVfPath(char* path, u32 size) {
    const std::string p = HomePath("wc24dl.vff");
    if (p.size() + 1 > size) {
        return NWC24_ERR_NOMEM;
    }
    std::memcpy(path, p.c_str(), p.size() + 1);
    return NWC24_OK;
}

// The download box: a folder at the .vff's NAND path (vf_host_revoex.cpp mounts it)
NWC24Err NWC24CreateDlVf(const NWC24DlTask* dlTask, u32 size) {
    (void)dlTask;
    (void)size;
    std::error_code ec;
    fs::create_directories(cf::sdk::NandHostPath(HomePath("wc24dl.vff").c_str()), ec);
    return ec ? NWC24_ERR_FILE_OTHER : NWC24_OK;
}

// The keys file, laid out as the Wii's: the RSA public key at 0, the AES key at 0x200
NWC24Err NWC24SetDlKeys(const void* key, const void* iv) {
    std::string data(0x210, '\0');
    std::memcpy(&data[0], key, 0x100);
    std::memcpy(&data[0x200], iv, 0x10);
    return WriteFile(cf::sdk::NandHostPath(HomePath("wc24pubk.mod").c_str()), data) ? NWC24_OK : NWC24_ERR_FILE_WRITE;
}

NWC24Err NWC24ClearDlKeys() {
    std::error_code ec;
    fs::remove(cf::sdk::NandHostPath(HomePath("wc24pubk.mod").c_str()), ec);
    return NWC24_OK;
}

} // extern "C"
