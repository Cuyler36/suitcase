#ifndef NW4R_UT_LOCKED_CACHE_H
#define NW4R_UT_LOCKED_CACHE_H
#include <nw4r/types_nw4r.h>

#include <revolution/os.h>

namespace nw4r {
namespace ut {
namespace LC {

void Enable();
void Disable();

bool Lock();
void Unlock();

void LoadBlocks(void* pDst, void* pSrc, ulong blocks);
void StoreBlocks(void* pDst, void* pSrc, ulong blocks);
void StoreData(void* pDst, void* pSrc, ulong size);

#if TARGET_PC
// No locked cache on PC: a buffer of its size (16 KiB) stands in for it. aurora's
// LCLoadBlocks/LCStoreBlocks copy to and from it.
inline void* GetBase() {
    alignas(32) static unsigned char sLockedCache[16 * 1024];
    return sLockedCache;
}
#else
inline void* GetBase() {
    return reinterpret_cast<void *>(LC_BASE);
}
#endif

inline void QueueWait(ulong len) {
    LCQueueWait(len);
}

inline void QueueWaitEx(ulong len) {
    while (LCQueueLength() != len) {
        OSYieldThread();
    }
}

} // namespace LC
} // namespace ut
} // namespace nw4r

#endif
