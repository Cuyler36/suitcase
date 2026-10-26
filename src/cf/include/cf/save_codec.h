// The save file (rvforest.dat) in the Wii's format on PC.
//
// The file is dSaveData_c's bytes in the Wii's layout, big-endian, with its bit-fields packed from
// the most significant bit. The PC keeps the save in memory as native structs (the layout matches the
// Wii's; d_nand.cpp static_asserts it) and converts at the file boundary (dNand::readFile /
// writeFile): byte order and bit-field order from the map that tools/gen_save_swap.py generates from
// the save structs' declarations, then the checksums.
//
// Every checksum in the save is a CRC-32 of raw bytes, so it differs between the two forms. Encoding
// recomputes the checksums of the structs in the range on the file bytes. Decoding checks them on the
// file bytes, then recomputes the valid ones on the native bytes (and leaves the invalid ones wrong),
// so the game's own checks see what they would see on a Wii.
//
// A range is a part of the file: [fileOffset, fileOffset + size). The game reads and writes whole
// structs, so a range never splits a value or a checksummed struct.
#pragma once

#include <types.h>

namespace cf {
namespace save {

// native: the range's bytes as the game has them; file: where the file's bytes go (may be native)
void Encode(u32 fileOffset, const void* native, void* file, u32 size);
// file: the range's bytes from the file; native: where the game's bytes go (may be file)
void Decode(u32 fileOffset, const void* file, void* native, u32 size);

// Decodes a save file and encodes it again: true if the bytes come back the same (logs the
// checksums that do not match and the bytes that differ). Run with CF_SAVE_SELFTEST=<file>.
bool SelfTest(const char* path);

} // namespace save
} // namespace cf
