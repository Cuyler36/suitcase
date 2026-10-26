#ifndef PRIVATE_CX_H
#define PRIVATE_CX_H

#include <revolution/types.h>

#define IN_BUFFER_AT(type_, buf_, offset_) (*((__typeof__(type_)*)((uintptr_t)(buf_) + (u32)(offset_))))

#define CXiConvertEndian32_ CXiConvertEndian_

// The compressed formats are little-endian (they come from the DS), so the Wii swaps and a
// little-endian host does not (the PC port)
static inline u32 CXiConvertEndian_(u32 x) {
#if TARGET_PC
    return x;
#else
    return ((x & 0xFF000000) >> 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8) | ((x & 0x000000FF) << 24);
#endif
}

static inline u16 CXiConvertEndian16_(u16 x) {
#if TARGET_PC
    return x;
#else
    return ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
#endif
}

BOOL CXiVerifyHuffmanTable_(void const*, u8);

#endif  // PRIVATE_CX_H
