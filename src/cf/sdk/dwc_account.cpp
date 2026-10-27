// DWC's account data (Nintendo Wi-Fi Connection): the player's own account record (DWCUserData, kept
// in each player's save as dFriendList_c::mUserData) and the friends' records (DWCFriendData,
// dFriendList_c::mKeys). On the Wii these are local DWC functions (lib/dwc/account/dwc_account.c);
// this is the same logic over the save's bytes, which stay in the Wii's byte order (the save codec
// keeps both records as raw bytes), so records from real saves and friend codes check out. The rest of
// DWC stays a stub (stubs_dwc.cpp).
//
// Port profile IDs: a player whose record has no Wi-Fi Connection profile ID (the account never logged
// in) has one derived from their pseudo login ID, at or above kPortProfileIdBase. WiiLink's server
// reserves that range (it never assigns it), so the port's IDs cannot collide with a real account, and
// they give every player a valid friend code without a login. Nothing is stored: a real profile ID in
// the record (a login, or a save from a Wii) replaces it. See docs/wc24/README.md.
//
// DWCUserData (0x40 bytes, big-endian words):
//   0x00 size (0x40)   0x04 pseudo login ID (3 words)   0x10 authentic login ID (3 words)
//   0x1C profile ID   0x20 flags (bit 0: dirty)   0x24 game code (DWC_Init's)   0x3C CRC
// DWCFriendData (0xC bytes): word 0 bits 11..31 flags (bits 0..1 the type: DWC_ACC_FRIENDDATA_*,
// bit 2 buddy), then by type: a login ID (word 0 bits 0..10 user ID high bits, word 1 user ID low,
// word 2 player ID), a friend key (word 1 low, word 2 high) or a profile ID (word 1).
#include <dwc/dwc.h>

#include <game/game/d_net_dwc.hpp>

#include "net_crypto.h"
#include "wc24_console.h"

#include <cf/wfc.h>

#include <cstring>
#include <random>

namespace {

constexpr u32 USER_DATA_SIZE = sizeof(DWCUserData);
constexpr u32 OFFSET_PSEUDO = 0x04;
constexpr u32 OFFSET_AUTHENTIC = 0x10;
constexpr u32 OFFSET_PROFILE_ID = 0x1C;
constexpr u32 OFFSET_FLAGS = 0x20;
constexpr u32 OFFSET_GAME_CODE = 0x24;
constexpr u32 OFFSET_CRC = 0x3C;
constexpr u32 FLAG_DIRTY = 1;
static_assert(USER_DATA_SIZE == 0x40, "DWCUserData size");
static_assert(sizeof(DWCFriendData) == 0xC, "DWCFriendData size");

// The data types of a login ID / friend data record (DWC_ACC_FRIENDDATA_*)
enum : u32 {
    TYPE_NONE = 0,
    TYPE_LOGIN_ID = 1,
    TYPE_FRIEND_KEY = 2,
    TYPE_PROFILE_ID = 3,
};
constexpr u32 TYPE_MASK = 3;
constexpr u32 FLAG_BUDDY = 1 << 2;
constexpr u32 FLAGS_SHIFT = 11;
constexpr u32 FLAGS_MASK = (1u << 21) - 1;
constexpr u32 USER_ID_HI_MASK = (1u << 11) - 1;

// The lowest port profile ID (WiiLink's reserved range: database/user.go)
constexpr u32 kPortProfileIdBase = 1000000000;

// DWC_Init's game code; City Folk passes dNetDwc::GAME_CODE before any record is made or checked
u32 sGameCode = dNetDwc::GAME_CODE;

u32 LoadBE32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3];
}

void StoreBE32(u8* p, u32 value) {
    p[0] = static_cast<u8>(value >> 24);
    p[1] = static_cast<u8>(value >> 16);
    p[2] = static_cast<u8>(value >> 8);
    p[3] = static_cast<u8>(value);
}

u8* Bytes(void* record) {
    return static_cast<u8*>(record);
}

const u8* Bytes(const void* record) {
    return static_cast<const u8*>(record);
}

// ---------------------------------------------------------------------------------------------
// Hashes: DWC's CRC-32 and CRC-8 (lib/dwc/common/dwci_np_math.c); MD5 is net_crypto.h's
// ---------------------------------------------------------------------------------------------

// CRC-32 (IEEE, reflected) of the record with each word byte-swapped: DWC's DWCi_Np_ToLE copy
u32 CalcUserDataCrc(const u8* data) {
    u8 swapped[USER_DATA_SIZE];
    for (u32 i = 0; i < USER_DATA_SIZE; i += 4) {
        swapped[i] = data[i + 3];
        swapped[i + 1] = data[i + 2];
        swapped[i + 2] = data[i + 1];
        swapped[i + 3] = data[i];
    }
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < OFFSET_CRC; i++) {
        crc ^= swapped[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

// CRC-8, polynomial 0x07, initial 0 (MATH_CalcCRC8 with MATH_CRC8_STANDARD_POLY)
u8 Crc8(const u8* data, u32 size) {
    u8 crc = 0;
    for (u32 i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = static_cast<u8>((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------------------------

// The flags of a login ID or friend data record (word 0)
u32 FlagsOf(const u8* record) {
    return (LoadBE32(record) >> FLAGS_SHIFT) & FLAGS_MASK;
}

u32 TypeOf(const u8* record) {
    return FlagsOf(record) & TYPE_MASK;
}

void SetFlags(u8* record, u32 flags) {
    const u32 word = LoadBE32(record);
    StoreBE32(record, (word & ~(FLAGS_MASK << FLAGS_SHIFT)) | ((flags & FLAGS_MASK) << FLAGS_SHIFT));
}

void SetType(u8* record, u32 type) {
    SetFlags(record, (FlagsOf(record) & ~TYPE_MASK) | type);
}

u64 FriendKeyOf(const u8* record) {
    return (static_cast<u64>(LoadBE32(record + 8)) << 32) | LoadBE32(record + 4);
}

void UpdateCrc(u8* userData) {
    StoreBE32(userData + OFFSET_CRC, CalcUserDataCrc(userData));
}

// A friend code: the profile ID and a 7-bit check of it with the game code (DWC_Acc_CreateFriendKey).
// The SDK hashes the two words in little-endian order; new codes use MD5, and CRC-8 ones are accepted.
u64 MakeFriendKey(int profileId, u32 gameCode, bool md5) {
    u8 code[8];
    for (int i = 0; i < 4; i++) {
        code[i] = static_cast<u8>(static_cast<u32>(profileId) >> (i * 8));
        code[4 + i] = static_cast<u8>(gameCode >> (i * 8));
    }
    u32 hash;
    if (md5) {
        u8 digest[16];
        cf::sdk::Md5(code, sizeof(code), digest);
        hash = digest[0] >> 1;
    } else {
        hash = Crc8(code, sizeof(code)) & 0x7F;
    }
    return (static_cast<u64>(hash) << 32) | static_cast<u32>(profileId);
}

bool CheckFriendKey(u64 friendKey, u32 gameCode, bool md5) {
    const int profileId = static_cast<int>(static_cast<u32>(friendKey));
    return profileId > 0 && MakeFriendKey(profileId, gameCode, md5) == friendKey;
}

// The port profile ID of a record without a real one: FNV-1a of the pseudo login ID's user and player
// IDs (not its flags), into [kPortProfileIdBase, 0x7FFFFFFF]
int PortProfileIdOf(const u8* userData) {
    const u8* pseudo = userData + OFFSET_PSEUDO;
    const u64 userId = (static_cast<u64>(LoadBE32(pseudo) & USER_ID_HI_MASK) << 32) | LoadBE32(pseudo + 4);
    const u32 playerId = LoadBE32(pseudo + 8);
    u64 hash = 0xcbf29ce484222325ull;
    for (int i = 0; i < 8; i++) {
        hash = (hash ^ static_cast<u8>(userId >> (i * 8))) * 0x100000001b3ull;
    }
    for (int i = 0; i < 4; i++) {
        hash = (hash ^ static_cast<u8>(playerId >> (i * 8))) * 0x100000001b3ull;
    }
    constexpr u32 range = 0x7FFFFFFFu - kPortProfileIdBase + 1;
    return static_cast<int>(kPortProfileIdBase + static_cast<u32>(hash % range));
}

// The player's profile ID: the record's (a Wi-Fi Connection login), else the port's
int ProfileIdOf(const u8* userData) {
    const int profileId = static_cast<int>(LoadBE32(userData + OFFSET_PROFILE_ID));
    return profileId > 0 ? profileId : PortProfileIdOf(userData);
}

} // namespace

extern "C" {

int DWC_Init(int authServer, const char* gameName, u32 gameCode, DWCAllocEx alloc, DWCFreeEx free) {
    (void)authServer;
    (void)gameName;
    (void)alloc;
    (void)free;
    sGameCode = gameCode;
    return -1; // no Wi-Fi Connection on PC
}

// ---------------------------------------------------------------------------------------------
// The player's record
// ---------------------------------------------------------------------------------------------

// A new record with a pseudo login ID (the console's DWC user ID and a random player ID) and no
// profile, as the SDK makes before the first login (DWCi_Acc_CreateUserData)
void DWC_CreateUserData(DWCUserData* userData) {
    u8* p = Bytes(userData);
    std::memset(p, 0, USER_DATA_SIZE);
    StoreBE32(p, USER_DATA_SIZE);
    StoreBE32(p + OFFSET_GAME_CODE, sGameCode);
    const u64 userId = cf::sdk::wc24::ConsoleDwcUserId();
    std::random_device random;
    u8* pseudo = p + OFFSET_PSEUDO;
    StoreBE32(pseudo, static_cast<u32>(userId >> 32) & USER_ID_HI_MASK);
    SetType(pseudo, TYPE_LOGIN_ID);
    StoreBE32(pseudo + 4, static_cast<u32>(userId));
    StoreBE32(pseudo + 8, random());
    StoreBE32(p + OFFSET_FLAGS, FLAG_DIRTY);
    UpdateCrc(p);
}

BOOL DWC_CheckUserData(const DWCUserData* userData) {
    const u8* p = Bytes(userData);
    return LoadBE32(p + OFFSET_CRC) == CalcUserDataCrc(p);
}

BOOL DWC_IsCompatibleUserData(const DWCUserData* userData) {
    return LoadBE32(Bytes(userData) + OFFSET_GAME_CODE) == sGameCode;
}

void DWC_ClearDirtyFlag(DWCUserData* userData) {
    u8* p = Bytes(userData);
    StoreBE32(p + OFFSET_FLAGS, LoadBE32(p + OFFSET_FLAGS) & ~FLAG_DIRTY);
    UpdateCrc(p);
}

void DWC_ReportUserData(DWCUserData* userData) {
    (void)userData;
}

// The player's friend code
u64 DWC_CreateFriendKey(const DWCUserData* userData) {
    const u8* p = Bytes(userData);
    return MakeFriendKey(ProfileIdOf(p), LoadBE32(p + OFFSET_GAME_CODE), true);
}

// The record the player's friends keep for them: their profile ID
void DWC_CreateExchangeToken(const DWCUserData* userData, DWCFriendData* friendData) {
    u8* token = Bytes(friendData);
    std::memset(token, 0, sizeof(DWCFriendData));
    SetType(token, TYPE_PROFILE_ID);
    StoreBE32(token + 4, static_cast<u32>(ProfileIdOf(Bytes(userData))));
}

int DWC_CheckFriendKey(const DWCUserData* userData, u64 friendKey) {
    return CheckFriendKey(friendKey, LoadBE32(Bytes(userData) + OFFSET_GAME_CODE), true);
}

// ---------------------------------------------------------------------------------------------
// Friends' records
// ---------------------------------------------------------------------------------------------

void DWC_CreateFriendKeyToken(DWCFriendData* friendData, u64 friendKey) {
    u8* token = Bytes(friendData);
    std::memset(token, 0, sizeof(DWCFriendData));
    SetType(token, TYPE_FRIEND_KEY);
    StoreBE32(token + 4, static_cast<u32>(friendKey));
    StoreBE32(token + 8, static_cast<u32>(friendKey >> 32));
}

BOOL DWC_IsValidFriendData(const DWCFriendData* friendData) {
    return TypeOf(Bytes(friendData)) != TYPE_NONE;
}

int DWC_GetFriendDataType(const DWCFriendData* friendData) {
    return static_cast<int>(TypeOf(Bytes(friendData)));
}

u64 DWC_GetFriendKey(const DWCFriendData* friendData) {
    const u8* p = Bytes(friendData);
    return TypeOf(p) == TYPE_FRIEND_KEY ? FriendKeyOf(p) : 0;
}

// A friend's profile ID: from their friend code, or the record's; -1 for a login ID, 0 for none
int DWC_GetGsProfileId(const DWCUserData* userData, const DWCFriendData* friendData) {
    const u8* p = Bytes(friendData);
    switch (TypeOf(p)) {
    case TYPE_FRIEND_KEY: {
        const u64 key = FriendKeyOf(p);
        const u32 gameCode = LoadBE32(Bytes(userData) + OFFSET_GAME_CODE);
        if (CheckFriendKey(key, gameCode, false) || CheckFriendKey(key, gameCode, true)) {
            return static_cast<int>(static_cast<u32>(key));
        }
        return 0;
    }
    case TYPE_PROFILE_ID:
        return static_cast<int>(LoadBE32(p + 4));
    case TYPE_LOGIN_ID:
        return -1;
    default:
        return 0;
    }
}

BOOL DWC_IsBuddyFriendData(const DWCFriendData* friendData) {
    const u8* p = Bytes(friendData);
    return TypeOf(p) == TYPE_PROFILE_ID && (FlagsOf(p) & FLAG_BUDDY) != 0;
}

void DWC_ClearBuddyFlagFriendData(DWCFriendData* friendData) {
    u8* p = Bytes(friendData);
    if (TypeOf(p) == TYPE_PROFILE_ID) {
        SetFlags(p, FlagsOf(p) & ~FLAG_BUDDY);
    }
}

BOOL DWC_IsEqualFriendData(const DWCFriendData* a, const DWCFriendData* b) {
    const u8* pa = Bytes(a);
    const u8* pb = Bytes(b);
    const u32 type = TypeOf(pa);
    if (type != TypeOf(pb)) {
        return FALSE;
    }
    switch (type) {
    case TYPE_PROFILE_ID:
        return LoadBE32(pa + 4) == LoadBE32(pb + 4);
    case TYPE_LOGIN_ID:
        return (LoadBE32(pa) & USER_ID_HI_MASK) == (LoadBE32(pb) & USER_ID_HI_MASK) &&
               LoadBE32(pa + 4) == LoadBE32(pb + 4) && LoadBE32(pa + 8) == LoadBE32(pb + 8);
    case TYPE_FRIEND_KEY:
        return FriendKeyOf(pa) == FriendKeyOf(pb);
    default:
        return FALSE;
    }
}

} // extern "C"

std::uint64_t cf::wfc::FriendCodeOf(int profileId, std::uint32_t gameCode) {
    return MakeFriendKey(profileId, gameCode, true);
}
