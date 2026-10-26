#ifndef NW4R_UT_LOCK_H
#define NW4R_UT_LOCK_H
#include <nw4r/types_nw4r.h>

#include <nw4r/ut/ut_NonCopyable.h>

#include <revolution/os.h>

namespace nw4r {
namespace ut {
namespace detail {

/******************************************************************************
 *
 * Mutex lock functions
 *
 ******************************************************************************/
inline void Lock(OSMutex& rMutex) {
    OSLockMutex(&rMutex);
}
inline void Unlock(OSMutex& rMutex) {
    OSUnlockMutex(&rMutex);
}

/******************************************************************************
 *
 * AutoLock
 *
 ******************************************************************************/
template <typename T> class AutoLock : private NonCopyable {
public:
    explicit AutoLock(T& rLockObj) : mLockObj(rLockObj) {
        Lock(mLockObj);
    }
    ~AutoLock() {
        Unlock(mLockObj);
    }

private:
    T& mLockObj; // at 0x0
};

} // namespace detail

/******************************************************************************
 *
 * AutoInterruptLock
 *
 ******************************************************************************/
#if TARGET_PC
} // namespace ut
} // namespace nw4r
namespace cf {
namespace sdk {
// The PC stand-in for disabled interrupts around nw4r's shared state: the software AX
// (src/cf/sdk/ax_rvl.cpp) runs its frame under it, as the Wii runs AX in interrupt context, and
// OSSleepThread releases it while sleeping (src/cf/sdk/os_thread.cpp).
void InterruptLockAcquire();
void InterruptLockRelease();
} // namespace sdk
} // namespace cf
namespace nw4r {
namespace ut {
#endif

class AutoInterruptLock : private NonCopyable {
public:
#if TARGET_PC
    AutoInterruptLock() : mOldState(OSDisableInterrupts()) {
        cf::sdk::InterruptLockAcquire();
    }
    ~AutoInterruptLock() {
        cf::sdk::InterruptLockRelease();
        OSRestoreInterrupts(mOldState);
    }
#else
    AutoInterruptLock() : mOldState(OSDisableInterrupts()) {}
    ~AutoInterruptLock() {
        OSRestoreInterrupts(mOldState);
    }
#endif

private:
    BOOL mOldState; // at 0x0
};

} // namespace ut
} // namespace nw4r

#endif
