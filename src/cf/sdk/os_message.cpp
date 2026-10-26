// OS message queues on PC, in place of the SDK's OSMessage.c (lib/RVL_SDK/src/os).
//
// The SDK checks a queue and sleeps on it with interrupts disabled, so a message sent in between
// cannot be missed. OSDisableInterrupts does nothing on PC, so OSMessage.c lost wakeups (the DVD
// command thread slept with a command queued, and the game waited on a black screen). Here the check
// and the sleep happen under the scheduler mutex (thread_system.h). Messages are pointer-sized.
#include <dolphin/os.h>

#include <mutex>

#include "interrupt_lock.h"
#include "thread_system.h"

namespace {

// Waits under the scheduler mutex until full() is false (or fails without OS_MESSAGE_BLOCK).
// Blocking releases the interrupt lock first: it is always taken before the scheduler mutex.
template <typename Full>
bool WaitUntilNot(std::unique_lock<std::mutex>& lock, OSThreadQueue* sleepQueue, s32 flags, Full full) {
    while (full()) {
        if (!(flags & OS_MESSAGE_BLOCK)) {
            return false;
        }
        lock.unlock();
        bool canceled = false;
        {
            cf::sdk::InterruptUnlockScope unlocked;
            lock.lock();
            if (full() && !cf::sdk::SleepLocked(lock, sleepQueue)) {
                canceled = true;
            }
            lock.unlock();
        }
        if (canceled) {
            cf::sdk::ThreadCancelPoint();
        }
        lock.lock();
    }
    return true;
}

} // namespace

extern "C" {

void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* msgArray, s32 msgCount) {
    OSInitThreadQueue(&queue->queueSend);
    OSInitThreadQueue(&queue->queueReceive);
    queue->msgArray = msgArray;
    queue->msgCount = msgCount;
    queue->firstIndex = 0;
    queue->usedCount = 0;
}

BOOL OSSendMessage(OSMessageQueue* queue, OSMessage msg, s32 flags) {
    std::mutex* sched = cf::sdk::GetSchedulerMutex();
    std::unique_lock<std::mutex> lock;
    if (sched != nullptr) {
        lock = std::unique_lock(*sched);
        if (!WaitUntilNot(lock, &queue->queueSend, flags, [queue] { return queue->usedCount >= queue->msgCount; })) {
            return FALSE;
        }
    } else if (queue->usedCount >= queue->msgCount) {
        return FALSE; // one thread is left: nothing could make room
    }
    const s32 last = (queue->firstIndex + queue->usedCount) % queue->msgCount;
    queue->msgArray[last] = msg;
    queue->usedCount++;
    cf::sdk::WakeupLocked(&queue->queueReceive);
    if (sched != nullptr) {
        lock.unlock();
        cf::sdk::GetSchedulerCv()->notify_all();
    }
    return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* msg, s32 flags) {
    std::mutex* sched = cf::sdk::GetSchedulerMutex();
    std::unique_lock<std::mutex> lock;
    if (sched != nullptr) {
        lock = std::unique_lock(*sched);
        if (!WaitUntilNot(lock, &queue->queueReceive, flags, [queue] { return queue->usedCount == 0; })) {
            return FALSE;
        }
    } else if (queue->usedCount == 0) {
        return FALSE;
    }
    if (msg != nullptr) {
        *msg = queue->msgArray[queue->firstIndex];
    }
    queue->firstIndex = (queue->firstIndex + 1) % queue->msgCount;
    queue->usedCount--;
    cf::sdk::WakeupLocked(&queue->queueSend);
    if (sched != nullptr) {
        lock.unlock();
        cf::sdk::GetSchedulerCv()->notify_all();
    }
    return TRUE;
}

// Not linked in City Folk; nw4r snd's SoundThread::Shutdown uses it on PC
BOOL OSJamMessage(OSMessageQueue* queue, OSMessage msg, s32 flags) {
    std::mutex* sched = cf::sdk::GetSchedulerMutex();
    std::unique_lock<std::mutex> lock;
    if (sched != nullptr) {
        lock = std::unique_lock(*sched);
        if (!WaitUntilNot(lock, &queue->queueSend, flags, [queue] { return queue->usedCount >= queue->msgCount; })) {
            return FALSE;
        }
    } else if (queue->usedCount >= queue->msgCount) {
        return FALSE;
    }
    queue->firstIndex = (queue->firstIndex + queue->msgCount - 1) % queue->msgCount;
    queue->msgArray[queue->firstIndex] = msg;
    queue->usedCount++;
    cf::sdk::WakeupLocked(&queue->queueReceive);
    if (sched != nullptr) {
        lock.unlock();
        cf::sdk::GetSchedulerCv()->notify_all();
    }
    return TRUE;
}

} // extern "C"
