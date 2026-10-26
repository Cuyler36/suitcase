// DWC user data (Nintendo Wi-Fi Connection): the console's own account record, which every player
// keeps in their save (dFriendList_c::mUserData). Making and checking it is local on the Wii (DWC
// fills in a pseudo login ID and a CRC; logging in replaces it later), so the PC does the same, in the
// Wii's format, so records from real saves check out: the save's validity check
// (dSaveData_c::isGood) rejects a player whose record does not. The rest of DWC stays a stub
// (stubs_dwc.cpp). Matches the DOL's lib/dwc/account (build/<version>/asm/lib/dwc/account).
//
// Layout (DWCAccUserData, 0x40 bytes, big-endian words):
//   0x00 size (0x40)   0x04..0x13 pseudo login ID   0x14..0x1F authentic login ID
//   0x20 flags (bit 0: dirty)   0x24 game code (DWC_Init's)   0x3C CRC
// The CRC is DWC's from the DS: CRC-32 of 0x00..0x3B with each 32-bit word byte-swapped.
#include <dwc/dwc.h>

#include <game/game/d_net_dwc.hpp>

#include <cstring>
#include <random>

namespace {

constexpr u32 USER_DATA_SIZE = sizeof(DWCUserData);
constexpr u32 OFFSET_PSEUDO_ID = 0x04;
constexpr u32 OFFSET_PSEUDO_USER_ID = 0x08;
constexpr u32 OFFSET_PSEUDO_PLAYER_ID = 0x10;
constexpr u32 OFFSET_FLAGS = 0x20;
constexpr u32 OFFSET_GAME_CODE = 0x24;
constexpr u32 OFFSET_CRC = 0x3C;
constexpr u32 FLAG_DIRTY = 1;
static_assert(USER_DATA_SIZE == 0x40, "DWCUserData size");

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

// DWC's CRC: MATH_CalcCRC32 (CRC-32, IEEE) of a copy with each word byte-swapped
u32 CalcCrc(const u8* data) {
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

u8* Bytes(DWCUserData* userData) {
    return reinterpret_cast<u8*>(userData);
}

const u8* Bytes(const DWCUserData* userData) {
    return reinterpret_cast<const u8*>(userData);
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

void DWC_CreateUserData(DWCUserData* userData) {
    u8* p = Bytes(userData);
    std::memset(p, 0, USER_DATA_SIZE);
    StoreBE32(p, USER_DATA_SIZE);
    StoreBE32(p + OFFSET_GAME_CODE, sGameCode);
    // A random pseudo account, as the SDK makes from the console's ID and the time before the first
    // login
    std::random_device random;
    StoreBE32(p + OFFSET_PSEUDO_ID, 0x800);
    StoreBE32(p + OFFSET_PSEUDO_USER_ID, random() & 0x7FF);
    StoreBE32(p + OFFSET_PSEUDO_USER_ID + 4, random());
    StoreBE32(p + OFFSET_PSEUDO_PLAYER_ID, random() & 0xFFFFF800);
    StoreBE32(p + OFFSET_CRC, CalcCrc(p));
}

BOOL DWC_CheckUserData(const DWCUserData* userData) {
    const u8* p = Bytes(userData);
    return LoadBE32(p + OFFSET_CRC) == CalcCrc(p);
}

BOOL DWC_IsCompatibleUserData(const DWCUserData* userData) {
    return LoadBE32(Bytes(userData) + OFFSET_GAME_CODE) == sGameCode;
}

void DWC_ClearDirtyFlag(DWCUserData* userData) {
    u8* p = Bytes(userData);
    StoreBE32(p + OFFSET_FLAGS, LoadBE32(p + OFFSET_FLAGS) & ~FLAG_DIRTY);
    StoreBE32(p + OFFSET_CRC, CalcCrc(p));
}

} // extern "C"
