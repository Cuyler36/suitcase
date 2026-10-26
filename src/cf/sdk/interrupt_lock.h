// The PC interrupt lock (src/cf/sdk/os_thread.cpp). OSDisableInterrupts stays a no-op; instead nw4r's
// ut::AutoInterruptLock (include/lib/nw4r/ut/ut_Lock.h) and the software AX frame (ax_rvl.cpp, the
// Wii's interrupt context) take this recursive lock. Anything that blocks (OSSleepThread,
// OSLockMutex, OSWaitCond, OSJoinThread) releases it while blocked, as sleeping on the Wii lets
// interrupts run.
#pragma once

namespace cf {
namespace sdk {

void InterruptLockAcquire();
void InterruptLockRelease();

// Releases all of this thread's holds (before blocking); returns the count for InterruptLockReacquire
int InterruptLockReleaseAll();
void InterruptLockReacquire(int count);

// Holds the interrupt lock for a scope
class InterruptLockScope {
public:
    InterruptLockScope() {
        InterruptLockAcquire();
    }
    ~InterruptLockScope() {
        InterruptLockRelease();
    }
    InterruptLockScope(const InterruptLockScope&) = delete;
    InterruptLockScope& operator=(const InterruptLockScope&) = delete;
};

// Releases the interrupt lock for a scope (around a blocking wait)
class InterruptUnlockScope {
public:
    InterruptUnlockScope() : mCount(InterruptLockReleaseAll()) {}
    ~InterruptUnlockScope() {
        InterruptLockReacquire(mCount);
    }
    InterruptUnlockScope(const InterruptUnlockScope&) = delete;
    InterruptUnlockScope& operator=(const InterruptUnlockScope&) = delete;

private:
    int mCount;
};

} // namespace sdk
} // namespace cf
