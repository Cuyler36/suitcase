// City Folk's peer traffic (town visits) in the Wii's format on PC.
//
// The game sends its structs raw: on a Wii they are big-endian, with bit-fields packed from the most
// significant bit. The PC keeps everything native and converts whole packets where they enter and leave
// the network (dNetMgr_c::send / onRecv): the packet and block headers, the shared records, every block
// and direct message by its id (save structs through the save codec, the rest by their layouts), and the
// Mii exchange block. The town transfer is the exception: its chunks cut through the save's structs, so
// the sender encodes the whole town once (EncodeTown) and the receiver decodes it once it has all of it
// (DecodeTown). See pc_port_endianness.md (E-65).
#pragma once

#include <types.h>

namespace cf {
namespace net {

// A packet the game is about to send to a member, in place (native to Wii)
void EncodePacket(void* data, u32 size, u32 member);
// A packet received from a DWC aid, in place (Wii to native): false when it is malformed or not the
// game's (drop it)
bool DecodePacket(void* data, u32 size, u32 aid);

// The town the host sends (dSaveTown_c, Wii format) and where the joiner's copy is decoded
const u8* EncodeTown(const void* town);
void DecodeTown(void* town);

// The friend status blob (dNetPlayerInfo::Info_c: UTF-16 names) as friends read it
void EncodeStatus(void* dst, const void* src, u32 size);

// CF_NET_LOG is set, and a line for its log
bool Logging();
void Log(const char* format, ...);

} // namespace net
} // namespace cf
