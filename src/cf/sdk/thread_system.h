// The PC thread layer's shared scheduling state (src/cf/sdk/os_thread.cpp), for the other OS files
// that wait (os_mutex.cpp, os_rvl.cpp).
//
// The Wii guards thread queues, thread states and mutex owners by disabling interrupts. The PC
// guards them with one scheduler mutex, and every wait in the layer is on the one scheduler condition
// variable (woken with notify_all, each waiter rechecking its own condition).
//
// When the game ends, StopGameThreads (shutdown.h) makes every wait on a game thread give up and
// unwind the thread, and DestroyThreadSystem frees this state. From then on only one thread is left
// and the getters return null: the OS calls do their bookkeeping without locking or waiting.
#pragma once

#include <dolphin/os.h>

#include <condition_variable>
#include <mutex>

namespace cf {
namespace sdk {

// Null after DestroyThreadSystem
std::mutex* GetSchedulerMutex();
std::condition_variable* GetSchedulerCv();

// Puts thread at the tail of queue as a waiting thread / takes it out (the scheduler mutex held)
void EnqueueWaitingThread(OSThreadQueue* queue, OSThread* thread);
void DequeueWaitingThread(OSThreadQueue* queue, OSThread* thread);

// With the scheduler mutex held (lock): sleeps on queue until woken, as OSSleepThread does with
// interrupts disabled, so a condition checked under the same lock cannot miss its wakeup. The
// caller has released the interrupt lock (it is taken before the scheduler mutex). Returns false if
// the calling game thread is being ended (then calls ThreadCancelPoint without the lock).
bool SleepLocked(std::unique_lock<std::mutex>& lock, OSThreadQueue* queue);
// With the scheduler mutex held: makes queue's threads ready; notify GetSchedulerCv() after
void WakeupLocked(OSThreadQueue* queue);

// Whether StopGameThreads is ending the calling thread (only game threads, from OSCreateThread, are
// ended). Waits use it in their predicates, then call ThreadCancelPoint without the scheduler mutex.
bool IsThreadCanceled();
// Unwinds the calling game thread to its entry (where it ends) if it is being ended
void ThreadCancelPoint();

// Leaves the game (os_rvl.cpp ExitGame): throws ExitRequested (shutdown.h) on the main thread, ends a
// game thread, and blocks a host thread until the host loop ends the game
[[noreturn]] void RequestExit();

} // namespace sdk
} // namespace cf
