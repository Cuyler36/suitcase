// Adapted from Dusklight (src/dusk/OSMutex.cpp, github.com/TwilitRealm/dusklight, CC0).
// OSMutex.cpp - PC implementation of the GameCube OSMutex/OSCond API
//
// As on the Wii, a mutex is its OSMutex fields (the owning thread and the recursion count) and a
// condition is its thread queue: OSWaitCond sleeps on cond->queue, OSSignalCond wakes it. The PC
// guards them with the scheduler mutex in place of disabled interrupts, and waits on the scheduler
// condition variable (thread_system.h), so there is no per-object native state.

#include <dolphin/dolphin.h>
#include <dolphin/os.h>

#include <mutex>
#include <condition_variable>

#include "interrupt_lock.h"
#include "thread_system.h"

namespace {

// Takes the mutex for thread if it is free or already thread's (the scheduler mutex held)
bool TryTake(OSMutex* mutex, OSThread* thread) {
    if (mutex->thread == nullptr) {
        mutex->thread = thread;
        mutex->count = 1;
        return true;
    }
    if (mutex->thread == thread) {
        mutex->count++;
        return true;
    }
    return false;
}

} // namespace

extern "C" {

void OSInitMutex(OSMutex* mutex) {
    if (!mutex) return;
    OSInitThreadQueue(&mutex->queue);
    mutex->thread = nullptr;
    mutex->count  = 0;
}

void OSLockMutex(OSMutex* mutex) {
    if (!mutex) return;

    OSThread* currentThread = OSGetCurrentThread();
    std::mutex* sched = cf::sdk::GetSchedulerMutex();
    if (sched == nullptr) {
        // One thread is left
        TryTake(mutex, currentThread);
        return;
    }
    {
        std::lock_guard lock(*sched);
        if (TryTake(mutex, currentThread)) {
            return;
        }
    }

    // Blocking: not with the interrupt lock held (the owner may need it to get to its unlock)
    {
        cf::sdk::InterruptUnlockScope unlocked;
        std::unique_lock lock(*sched);
        cf::sdk::GetSchedulerCv()->wait(lock, [mutex, currentThread] {
            return TryTake(mutex, currentThread) || cf::sdk::IsThreadCanceled();
        });
    }
    cf::sdk::ThreadCancelPoint();
}

void OSUnlockMutex(OSMutex* mutex) {
    if (!mutex) return;

    OSThread* currentThread = OSGetCurrentThread();
    std::mutex* sched = cf::sdk::GetSchedulerMutex();
    std::unique_lock<std::mutex> lock;
    if (sched != nullptr) {
        lock = std::unique_lock(*sched);
    }

    if (mutex->thread != currentThread) return;
    if (--mutex->count == 0) {
        mutex->thread = nullptr;
        if (sched != nullptr) {
            lock.unlock();
            cf::sdk::GetSchedulerCv()->notify_all();
        }
    }
}

BOOL OSTryLockMutex(OSMutex* mutex) {
    if (!mutex) return FALSE;

    OSThread* currentThread = OSGetCurrentThread();
    std::mutex* sched = cf::sdk::GetSchedulerMutex();
    std::unique_lock<std::mutex> lock;
    if (sched != nullptr) {
        lock = std::unique_lock(*sched);
    }
    return TryTake(mutex, currentThread) ? TRUE : FALSE;
}

// ============================================================================
// Internal: unlock all mutexes held by a thread (called on thread exit)
// ============================================================================

void __OSUnlockAllMutex(OSThread* thread) {
    // On GC this walks the thread's mutex queue, which the PC does not keep.
    if (!thread) return;
    thread->queueMutex.head = nullptr;
    thread->queueMutex.tail = nullptr;
}

int __OSCheckDeadLock(OSThread* thread) {
    return 0;
}

int __OSCheckMutexes(OSThread* thread) {
    return 1;
}

// ============================================================================
// Condition Variable API
// ============================================================================

void OSInitCond(OSCond* cond) {
    if (!cond) return;
    OSInitThreadQueue(&cond->queue);
}

void OSWaitCond(OSCond* cond, OSMutex* mutex) {
    if (!cond || !mutex) return;

    OSThread* currentThread = OSGetCurrentThread();
    std::mutex* sched = cf::sdk::GetSchedulerMutex();
    if (sched == nullptr) {
        // One thread is left: nothing could signal it
        return;
    }
    std::condition_variable* cv = cf::sdk::GetSchedulerCv();

    {
        // Not with the interrupt lock held while waiting
        cf::sdk::InterruptUnlockScope unlocked;
        std::unique_lock lock(*sched);

        // Release the mutex completely and sleep on the condition, in one step (no signal is lost)
        const s32 count = mutex->thread == currentThread ? mutex->count : 0;
        if (count > 0) {
            mutex->thread = nullptr;
            mutex->count = 0;
        }
        cf::sdk::EnqueueWaitingThread(&cond->queue, currentThread);
        cv->notify_all();
        cv->wait(lock, [currentThread] {
            return currentThread->state != OS_THREAD_STATE_WAITING || cf::sdk::IsThreadCanceled();
        });
        if (currentThread->state == OS_THREAD_STATE_WAITING) {
            cf::sdk::DequeueWaitingThread(&cond->queue, currentThread);
        }

        // Take the mutex back with its recursion count
        if (count > 0) {
            cv->wait(lock, [mutex] { return mutex->thread == nullptr || cf::sdk::IsThreadCanceled(); });
            if (mutex->thread == nullptr) {
                mutex->thread = currentThread;
                mutex->count = count;
            }
        }
    }
    cf::sdk::ThreadCancelPoint();
}

void OSSignalCond(OSCond* cond) {
    if (!cond) return;
    OSWakeupThread(&cond->queue);
}

#ifdef __cplusplus
}
#endif
