#ifndef NW4R_G3D_PLATFORM_CPU_H
#define NW4R_G3D_PLATFORM_CPU_H
#include <port/endian.h>
#include <nw4r/types_nw4r.h>

#include <revolution/os.h>

namespace nw4r {
namespace g3d {

/******************************************************************************
 *
 * Fastcast
 *
 ******************************************************************************/
namespace fastcast {

/******************************************************************************
 *
 * Convert from U8
 *
 ******************************************************************************/
inline f32 U8_0ToF32(const u8* pPtr) {
    f32 x;
    OSu8tof32(const_cast<u8*>(pPtr), &x);
    return x;
}

/******************************************************************************
 *
 * Convert from U16
 *
 ******************************************************************************/
inline f32 U16_0ToF32(const u16* pPtr) {
    f32 x;
    OSu16tof32(const_cast<u16*>(pPtr), &x);
    return x;
}

/******************************************************************************
 *
 * Convert from S16
 *
 ******************************************************************************/
#if TARGET_PC
// psq_l through GQR7 (S16, scale 8): fixed point S7.8
inline f32 S7_8ToF32(const s16* pPtr) {
    return *pPtr * (1.0f / 256.0f);
}

// psq_l through GQR6 (S16, scale 5): fixed point S10.5
inline f32 S10_5ToF32(const s16* pPtr) {
    return *pPtr * (1.0f / 32.0f);
}

// The same for big-endian resource data (E-1 in pc_port_endianness.md)
inline f32 U16_0ToF32(const BE(u16)* pPtr) {
    return static_cast<f32>(static_cast<u16>(*pPtr));
}
inline f32 S7_8ToF32(const BE(s16)* pPtr) {
    return static_cast<s16>(*pPtr) * (1.0f / 256.0f);
}
inline f32 S10_5ToF32(const BE(s16)* pPtr) {
    return static_cast<s16>(*pPtr) * (1.0f / 32.0f);
}
#else
inline f32 S7_8ToF32(register const s16* pPtr) {
    register f32 f;

    // clang-format off
    asm {
        psq_l f, 0(pPtr), 1, 7
    }
    // clang-format on

    return f;
}

inline f32 S10_5ToF32(register const s16* pPtr) {
    register f32 f;

    // clang-format off
    asm {
        psq_l f, 0(pPtr), 1, 6
    }
    // clang-format on

    return f;
}
#endif

/******************************************************************************
 *
 * Convert from F32
 *
 ******************************************************************************/
inline u8 F32ToU8_0(f32 f) {
    u8 x;
    OSf32tou8(&f, &x);
    return x;
}

#if TARGET_PC
// psq_st through GQR6 (S16, scale 5): scale by 2^5, saturate to s16, truncate toward zero
inline s16 F32ToS10_5(f32 f) {
    f32 scaled = f * 32.0f;
    if (scaled >= 32767.0f) {
        return 32767;
    }
    if (scaled <= -32768.0f) {
        return -32768;
    }
    return static_cast<s16>(scaled);
}
#else
inline s16 F32ToS10_5(register f32 f) {
    s16 x;
    register s16* pPtr = &x;

    // clang-format off
    asm {
        psq_st f, 0(pPtr), 1, 6
    }
    // clang-format on

    return x;
}
#endif

/******************************************************************************
 *
 * GQR
 *
 ******************************************************************************/
inline void SetGQR6_S10_5() {
    OSSetGQR6(OS_GQR_S16, 5);
}
inline void SetGQR7_S7_8() {
    OSSetGQR7(OS_GQR_S16, 8);
}

/******************************************************************************
 *
 * Initialization
 *
 ******************************************************************************/
namespace detail {

inline void Init() {
    OSInitFastCast();
    SetGQR6_S10_5();
    SetGQR7_S7_8();
}

} // namespace detail
} // namespace fastcast

/******************************************************************************
 *
 * Cache
 *
 ******************************************************************************/
namespace DC {

inline void StoreRange(void* pBase, ulong size) {
    DCStoreRange(pBase, size);
}

inline void StoreRangeNoSync(void* pBase, ulong size) {
    DCStoreRangeNoSync(pBase, size);
}

inline void FlushRangeNoSync(void* pBase, ulong size) {
    DCFlushRangeNoSync(pBase, size);
}

inline void InvalidateRange(void* pBase, ulong size) {
    DCInvalidateRange(pBase, size);
}

} // namespace DC

/******************************************************************************
 *
 * Memory
 *
 ******************************************************************************/
namespace detail {

void Copy32ByteBlocks(void* pDst, const void* pSrc, ulong size);
void ZeroMemory32ByteBlocks(void* pDst, ulong size);

} // namespace detail

/******************************************************************************
 *
 * Initialize fastcast
 *
 ******************************************************************************/
inline void InitFastCast() {
    fastcast::detail::Init();
}

} // namespace g3d
} // namespace nw4r

#endif
