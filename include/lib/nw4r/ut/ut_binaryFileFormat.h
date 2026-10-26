#ifndef NW4R_UT_BINARY_FILE_FORMAT_H
#define NW4R_UT_BINARY_FILE_FORMAT_H
#include <nw4r/types_nw4r.h>
#include <port/endian.h>

namespace nw4r {
namespace ut {

// Big-endian in every file (BE() converts on PC)
struct BinaryBlockHeader {
    BE(u32) kind; // at 0x0
    BE(u32) size; // at 0x4
};

struct BinaryFileHeader {
    BE(u32) signature;  // at 0x0
    BE(u16) byteOrder;  // at 0x4
    BE(u16) version;    // at 0x6
    BE(u32) fileSize;   // at 0x8
    BE(u16) headerSize; // at 0xC
    BE(u16) dataBlocks; // at 0xE
};

bool IsValidBinaryFile(const BinaryFileHeader* pHeader, ulong signature,
                       u16 version, u16 minBlocks);

} // namespace ut
} // namespace nw4r

#endif
