// Adapted from Dusklight (src/dusk/OSThread.cpp, github.com/TwilitRealm/dusklight, CC0).
// OSThread.cpp - PC implementation of GameCube OSThread API
// Maps GameCube cooperative threading to native OS threads via std::thread.
// The OSThread struct layout is preserved so game code can read its fields.
// A side-table stores the native std::thread per OSThread.
//
// The layer's state is one ThreadSystem (thread_system.h): created on first use, freed by
// DestroyThreadSystem when the game has ended (shutdown.h). Game threads end by returning, by
// OSExitThread, or when StopGameThreads cancels them: each is a C++ exception unwound to
// ThreadEntryWrapper.

#include <dolphin/dolphin.h>
#include <dolphin/os.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include <borealis/log_c.h>

#include "interrupt_lock.h"
#include "shutdown.h"
#include "thread_system.h"

#if _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#elif __APPLE__
#include <pthread.h>
#endif

namespace {

// ============================================================================
// State
// ============================================================================

// The native side of an OSThread. started/suspended/finished are guarded by the scheduler mutex.
struct PCThreadData {
    std::thread nativeThread;
    void* (*func)(void*);
    void* param;
    bool started   = false;
    bool suspended = false;
    bool finished  = false; // ThreadEntryWrapper is done with the OSThread
};

struct ThreadSystem {
    // PCThreadData per OSThread
    std::mutex mapMutex;
    std::unordered_map<OSThread*, std::unique_ptr<PCThreadData>> threads;

    // What the Wii guards by disabling interrupts: thread queues and states, suspends, mutex owners
    // (thread_system.h)
    std::mutex schedMutex;
    std::condition_variable schedCv;

    // The interrupt lock (interrupt_lock.h)
    std::recursive_mutex interruptMutex;
};

ThreadSystem* sSystem = nullptr;
bool sSystemDestroyed = false;
std::once_flag sSystemOnce;

// Null once destroyed (only one thread is left then)
ThreadSystem* GetSystem() {
    if (sSystemDestroyed) {
        return nullptr;
    }
    std::call_once(sSystemOnce, [] { sSystem = new ThreadSystem; });
    return sSystem;
}

PCThreadData* GetThreadData(ThreadSystem* sys, OSThread* thread) {
    std::lock_guard mapLock(sys->mapMutex);
    auto it = sys->threads.find(thread);
    return it != sys->threads.end() ? it->second.get() : nullptr;
}

// How a game thread's function is left early (unwound to ThreadEntryWrapper)
struct ThreadExit {
    void* val;
};
struct ThreadCanceled {};

// Set by StopGameThreads; game threads (tls_gameThread) then end at their next wait
std::atomic<bool> sCanceling{false};
// Some game thread did not end, so the state stays for it
bool sStragglers = false;
std::atomic<bool> sExitRequested{false};

thread_local OSThread* tls_currentThread = nullptr;
// Started by OSResumeThread (not the main thread or a host thread of the SDK layer or aurora)
thread_local bool tls_gameThread = false;
// The OSThread of a host thread that is not a game thread (the audio device, alarms, DVD, NAND)
thread_local OSThread tls_hostThread;

OSThread  sDefaultThread;
u8        sDefaultStack[64 * 1024];
u8        sDefaultStackEnd = OS_THREAD_STACK_MAGIC;
bool      sThreadInitDone = false;

thread_local int sInterruptLockCount = 0;

std::atomic<s32> sSchedulerSuspendCount{0};
std::atomic<s32> sActiveThreadCount{0};
OSSwitchThreadCallback sSwitchThreadCallback = nullptr;

void InitThreadFields(OSThread* thread, OSPriority priority, u16 attr) {
    memset(thread, 0, sizeof(OSThread));
    thread->attr     = attr;
    thread->base     = priority;
    thread->priority = priority;
    thread->val      = (void*)(intptr_t)-1;
    OSInitThreadQueue(&thread->queueJoin);
    OSClearContext(&thread->context);
}

// Thread entry wrapper - runs on the new std::thread
void ThreadEntryWrapper(OSThread* thread, PCThreadData* data) {
    tls_currentThread = thread;
    tls_gameThread = true;

    OSClearContext(&thread->context);
    OSSetCurrentContext(&thread->context);

    thread->state = OS_THREAD_STATE_RUNNING;

    void* result = nullptr;
    try {
        result = data->func(data->param);
    } catch (const ThreadExit& exit) {
        result = exit.val;
    } catch (const ThreadCanceled&) {
        // StopGameThreads ended it
    }

    // OSExitThread's bookkeeping
    thread->val = result;
    thread->state = (thread->attr & OS_THREAD_ATTR_DETACH) ? 0 : OS_THREAD_STATE_MORIBUND;
    OSWakeupThread(&thread->queueJoin);
    sActiveThreadCount--;

    if (ThreadSystem* sys = GetSystem()) {
        {
            std::lock_guard lock(sys->schedMutex);
            data->finished = true;
        }
        sys->schedCv.notify_all();
    }
}

// Waits on the scheduler until done() (the scheduler mutex held, the interrupt lock not), giving
// up if the calling game thread is being ended; then throws for that
template <typename Done>
void SchedulerWait(ThreadSystem* sys, Done done) {
    {
        cf::sdk::InterruptUnlockScope unlocked;
        std::unique_lock lock(sys->schedMutex);
        sys->schedCv.wait(lock, [&] { return done() || cf::sdk::IsThreadCanceled(); });
    }
    cf::sdk::ThreadCancelPoint();
}

} // namespace

// ============================================================================
// thread_system.h / interrupt_lock.h / shutdown.h
// ============================================================================

namespace cf {
namespace sdk {

std::mutex* GetSchedulerMutex() {
    ThreadSystem* sys = GetSystem();
    return sys != nullptr ? &sys->schedMutex : nullptr;
}

std::condition_variable* GetSchedulerCv() {
    ThreadSystem* sys = GetSystem();
    return sys != nullptr ? &sys->schedCv : nullptr;
}

void EnqueueWaitingThread(OSThreadQueue* queue, OSThread* thread) {
    thread->state = OS_THREAD_STATE_WAITING;
    thread->queue = queue;
    OSThread* prev = queue->tail;
    if (prev == nullptr) {
        queue->head = thread;
    } else {
        prev->link.next = thread;
    }
    thread->link.prev = prev;
    thread->link.next = nullptr;
    queue->tail = thread;
}

void DequeueWaitingThread(OSThreadQueue* queue, OSThread* thread) {
    OSThread* next = thread->link.next;
    OSThread* prev = thread->link.prev;
    if (next == nullptr) {
        queue->tail = prev;
    } else {
        next->link.prev = prev;
    }
    if (prev == nullptr) {
        queue->head = next;
    } else {
        prev->link.next = next;
    }
    thread->link.next = thread->link.prev = nullptr;
    thread->queue = nullptr;
    thread->state = OS_THREAD_STATE_RUNNING;
}

bool SleepLocked(std::unique_lock<std::mutex>& lock, OSThreadQueue* queue) {
    ThreadSystem* sys = GetSystem();
    OSThread* current = OSGetCurrentThread();
    EnqueueWaitingThread(queue, current);
    sys->schedCv.wait(lock, [current] { return current->state != OS_THREAD_STATE_WAITING || IsThreadCanceled(); });
    if (current->state == OS_THREAD_STATE_WAITING) {
        DequeueWaitingThread(queue, current);
        return false;
    }
    return true;
}

void WakeupLocked(OSThreadQueue* queue) {
    OSThread* thread = queue->head;
    while (thread) {
        OSThread* next = thread->link.next;
        thread->state = OS_THREAD_STATE_READY;
        thread->link.next = nullptr;
        thread->link.prev = nullptr;
        thread->queue = nullptr;
        thread = next;
    }
    queue->head = queue->tail = nullptr;
}

bool IsThreadCanceled() {
    return tls_gameThread && sCanceling.load();
}

void ThreadCancelPoint() {
    if (IsThreadCanceled()) {
        throw ThreadCanceled{};
    }
}

void InterruptLockAcquire() {
    if (ThreadSystem* sys = GetSystem()) {
        sys->interruptMutex.lock();
    }
    sInterruptLockCount++;
}

void InterruptLockRelease() {
    sInterruptLockCount--;
    if (ThreadSystem* sys = GetSystem()) {
        sys->interruptMutex.unlock();
    }
}

int InterruptLockReleaseAll() {
    int count = sInterruptLockCount;
    for (int i = 0; i < count; i++) {
        InterruptLockRelease();
    }
    return count;
}

void InterruptLockReacquire(int count) {
    for (int i = 0; i < count; i++) {
        InterruptLockAcquire();
    }
}

bool IsExitRequested() {
    return sExitRequested.load();
}

// os_rvl.cpp's ExitGame: unwinds the main thread to the host loop, ends a game thread
[[noreturn]] void RequestExit() {
    sExitRequested = true;
    if (tls_currentThread == &sDefaultThread) {
        throw ExitRequested{};
    }
    if (tls_gameThread) {
        throw ThreadCanceled{};
    }
    // A host thread of the SDK layer: the host loop ends the game; this one waits for the end
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool StopGameThreads(std::chrono::milliseconds timeout) {
    ThreadSystem* sys = GetSystem();
    if (sys == nullptr) {
        return true;
    }
    {
        std::lock_guard lock(sys->schedMutex);
        sCanceling = true;
    }
    sys->schedCv.notify_all();

    std::vector<std::pair<OSThread*, PCThreadData*>> started;
    {
        std::lock_guard mapLock(sys->mapMutex);
        for (auto& [thread, data] : sys->threads) {
            if (data->nativeThread.joinable()) {
                started.emplace_back(thread, data.get());
            }
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool all = true;
    for (auto& [thread, data] : started) {
        bool finished;
        {
            std::unique_lock lock(sys->schedMutex);
            finished = sys->schedCv.wait_until(lock, deadline, [data = data] { return data->finished; });
        }
        if (finished) {
            data->nativeThread.join();
        } else {
            borealis_log_printf(BOREALIS_LOG_WARNING, "cf::os", "game thread %p did not end", thread);
            all = false;
        }
    }
    sStragglers = !all;
    return all;
}

void DestroyThreadSystem() {
    if (sSystem == nullptr || sSystemDestroyed) {
        return;
    }
    if (sStragglers) {
        // A game thread that did not end may still use it; the process ends it
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::os", "thread layer kept for the game threads still running");
        for (auto& [thread, data] : sSystem->threads) {
            if (data->nativeThread.joinable()) {
                data->nativeThread.detach();
            }
        }
        return;
    }
    sSystemDestroyed = true;
    delete sSystem;
    sSystem = nullptr;
}

} // namespace sdk
} // namespace cf

// ============================================================================
// C API functions
// ============================================================================

extern "C" {

void __OSThreadInit(void) {
    InitThreadFields(&sDefaultThread, 16, OS_THREAD_ATTR_DETACH);
    sDefaultThread.state = OS_THREAD_STATE_RUNNING;

    // Stack pointers (JKRThread reads these)
    sDefaultThread.stackBase = sDefaultStack + sizeof(sDefaultStack);
    sDefaultThread.stackEnd  = &sDefaultStackEnd;
    sDefaultStackEnd = OS_THREAD_STACK_MAGIC;

    // Set as current thread for main thread
    tls_currentThread = &sDefaultThread;
    sThreadInitDone = true;

    // Active queue
    sActiveThreadCount = 1;

    OSReport("[PC-OSThread] Thread system initialized (multi-threaded mode)\n");
}

// ============================================================================
// Thread Queue
// ============================================================================

void OSInitThreadQueue(OSThreadQueue* queue) {
    if (queue) {
        queue->head = queue->tail = nullptr;
    }
}

// ============================================================================
// Current Thread
// ============================================================================

OSThread* OSGetCurrentThread(void) {
    if (tls_currentThread == nullptr) {
        if (!sThreadInitDone) {
            // The main thread, at boot
            __OSThreadInit();
        } else {
            // A host thread calling into the SDK (the audio device, alarms, DVD, NAND): its own
            // OSThread, so mutex ownership tells it apart from the game's threads
            InitThreadFields(&tls_hostThread, 16, OS_THREAD_ATTR_DETACH);
            tls_hostThread.state = OS_THREAD_STATE_RUNNING;
            tls_currentThread = &tls_hostThread;
        }
    }
    return tls_currentThread;
}

// ============================================================================
// Thread Creation
// ============================================================================

int OSCreateThread(OSThread* thread, void* (*func)(void*), void* param,
                   void* stack, u32 stackSize, OSPriority priority, u16 attr) {
    if (!thread) return 0;
    if (priority < OS_PRIORITY_MIN || priority > OS_PRIORITY_MAX) return 0;

    // Ensure thread system is initialized
    OSGetCurrentThread();
    ThreadSystem* sys = GetSystem();
    if (sys == nullptr) return 0;

    InitThreadFields(thread, priority, attr & 1u);
    thread->state   = OS_THREAD_STATE_READY;
    thread->suspend = 1;  // Created suspended (GC behavior)

    // Stack (stack points to TOP on GameCube)
    thread->stackBase = (u8*)stack;
    thread->stackEnd  = (u8*)((uintptr_t)stack - stackSize);
    *thread->stackEnd = OS_THREAD_STACK_MAGIC;

    // Create side-table entry (but don't start the thread yet)
    auto data = std::make_unique<PCThreadData>();
    data->func  = func;
    data->param = param;

    std::unique_ptr<PCThreadData> old;
    {
        std::lock_guard mapLock(sys->mapMutex);
        std::swap(sys->threads[thread], old);
        sys->threads[thread] = std::move(data);
    }
    // An OSThread may be created again once it has terminated (dKbd::sThread for every keyboard).
    // Its last host thread has returned from (or is leaving) ThreadEntryWrapper, and destroying a
    // still-joinable std::thread calls std::terminate
    if (old && old->nativeThread.joinable()) {
        if (old->nativeThread.get_id() == std::this_thread::get_id()) {
            old->nativeThread.detach();
        } else {
            old->nativeThread.join();
        }
    }

    sActiveThreadCount++;

    OSReport("[PC-OSThread] Created thread %p (priority=%d, stackSize=%u)\n",
             thread, priority, stackSize);
    return 1;
}

// ============================================================================
// Resume / Suspend
// ============================================================================

s32 OSResumeThread(OSThread* thread) {
    if (!thread)
        return 0;
    ThreadSystem* sys = GetSystem();
    if (sys == nullptr)
        return 0;
    PCThreadData* data = GetThreadData(sys, thread);

    s32 prevSuspend;
    bool launch = false;
    {
        std::lock_guard lock(sys->schedMutex);
        prevSuspend = thread->suspend;
        if (thread->suspend > 0) {
            thread->suspend--;
        }
        // Only wake up if suspend count drops to 0
        if (thread->suspend == 0 && data != nullptr) {
            if (!data->started) {
                data->started = true;
                launch = true;
            }
            data->suspended = false;
        }
    }

    if (launch) {
        // First resume: launch the native thread
        data->nativeThread = std::thread(ThreadEntryWrapper, thread, data);
        OSReport("[PC-OSThread] Started thread %p\n", thread);
    } else {
        sys->schedCv.notify_all();
    }
    return prevSuspend;
}

s32 OSSuspendThread(OSThread* thread) {
    if (!thread)
        return 0;
    ThreadSystem* sys = GetSystem();
    if (sys == nullptr)
        return 0;
    PCThreadData* data = GetThreadData(sys, thread);

    s32 prevSuspend;
    {
        std::lock_guard lock(sys->schedMutex);
        prevSuspend = thread->suspend++;
        if (prevSuspend == 0 && data != nullptr && data->started) {
            data->suspended = true;
        }
    }

    // A thread suspending itself waits here until resumed, as OSSuspendThread yields on the Wii.
    // Suspending another thread only sets the flag: a std::thread cannot be stopped from outside.
    if (prevSuspend == 0 && data != nullptr && data->started && thread == OSGetCurrentThread()) {
        SchedulerWait(sys, [data] { return !data->suspended; });
    }
    return prevSuspend;
}

// ============================================================================
// Sleep / Wakeup (thread queue based)
// ============================================================================

void OSSleepThread(OSThreadQueue* queue) {
    if (!queue) return;

    OSThread* currentThread = OSGetCurrentThread();
    ThreadSystem* sys = GetSystem();
    if (sys == nullptr) {
        // One thread is left: nothing could wake it
        return;
    }

    // Not with the interrupt lock held while waiting (a wakeup in between is seen by the predicate)
    {
        cf::sdk::InterruptUnlockScope unlocked;
        std::unique_lock lock(sys->schedMutex);
        cf::sdk::EnqueueWaitingThread(queue, currentThread);
        sys->schedCv.wait(lock, [currentThread] {
            return currentThread->state != OS_THREAD_STATE_WAITING || cf::sdk::IsThreadCanceled();
        });
        if (currentThread->state == OS_THREAD_STATE_WAITING) {
            cf::sdk::DequeueWaitingThread(queue, currentThread);
        }
    }
    cf::sdk::ThreadCancelPoint();
}

void OSWakeupThread(OSThreadQueue* queue) {
    if (!queue) return;

    ThreadSystem* sys = GetSystem();
    std::unique_lock<std::mutex> lock;
    if (sys != nullptr) {
        lock = std::unique_lock(sys->schedMutex);
    }

    // Wake all threads in the queue
    cf::sdk::WakeupLocked(queue);

    if (sys != nullptr) {
        lock.unlock();
        sys->schedCv.notify_all();
    }
}

// ============================================================================
// Exit / Cancel / Detach / Join
// ============================================================================

void OSExitThread(void* val) {
    if (tls_gameThread) {
        // Does not return, as on the Wii: ThreadEntryWrapper does the bookkeeping
        throw ThreadExit{val};
    }
    OSReport("[PC-OSThread] OSExitThread outside a game thread is ignored\n");
}

void OSCancelThread(OSThread* thread) {
    // A std::thread cannot be cancelled: mark the OSThread finished; the host thread keeps running
    // (and does ThreadEntryWrapper's bookkeeping again when it returns)
    OSReport("[PC-OSThread] OSCancelThread: the host thread is not stopped\n");
    if (!thread) return;

    if (thread->attr & OS_THREAD_ATTR_DETACH) {
        thread->state = 0;
    } else {
        thread->state = OS_THREAD_STATE_MORIBUND;
    }

    OSWakeupThread(&thread->queueJoin);
}

void OSDetachThread(OSThread* thread) {
    if (!thread) return;
    thread->attr |= OS_THREAD_ATTR_DETACH;

    if (thread->state == OS_THREAD_STATE_MORIBUND) {
        thread->state = 0;
    }
    OSWakeupThread(&thread->queueJoin);
}

BOOL OSJoinThread(OSThread* thread, void** val) {
    if (!thread) return 0;

    ThreadSystem* sys = GetSystem();
    if (sys != nullptr && !(thread->attr & OS_THREAD_ATTR_DETACH)) {
        PCThreadData* data = GetThreadData(sys, thread);
        if (data != nullptr && data->nativeThread.joinable()) {
            // Not with the interrupt lock held (the thread may need it to finish)
            SchedulerWait(sys, [data] { return data->finished; });
            data->nativeThread.join();
        }
    }

    if (thread->state == OS_THREAD_STATE_MORIBUND) {
        if (val) {
            *val = thread->val;
        }
        thread->state = 0;

        if (sys != nullptr) {
            std::lock_guard mapLock(sys->mapMutex);
            sys->threads.erase(thread);
        }
        return 1;
    }
    return 0;
}

// ============================================================================
// Yield / Terminated / Active
// ============================================================================

void OSYieldThread(void) {
    std::this_thread::yield();
}

BOOL OSIsThreadSuspended(OSThread* thread) {
    return (thread && thread->suspend > 0) ? TRUE : FALSE;
}

BOOL OSIsThreadTerminated(OSThread* thread) {
    if (!thread) return TRUE;
    return (thread->state == OS_THREAD_STATE_MORIBUND || thread->state == 0) ? TRUE : FALSE;
}

s32 OSCheckActiveThreads(void) {
    return sActiveThreadCount.load();
}

// ============================================================================
// Priority
// ============================================================================

int OSSetThreadPriority(OSThread* thread, OSPriority priority) {
    if (!thread) return 0;
    if (priority < OS_PRIORITY_MIN || priority > OS_PRIORITY_MAX) return 0;
    thread->base = priority;
    thread->priority = priority;
    return 1;
}

s32 OSGetThreadPriority(OSThread* thread) {
    if (!thread) return 16;
    return thread->base;
}

// ============================================================================
// Switch Thread Callback
// ============================================================================

OSSwitchThreadCallback OSSetSwitchThreadCallback(OSSwitchThreadCallback callback) {
    OSSwitchThreadCallback prev = sSwitchThreadCallback;
    sSwitchThreadCallback = callback;
    return prev;
}

// ============================================================================
// Scheduler (atomic counter, no real effect with native OS threads)
// ============================================================================

s32 OSDisableScheduler(void) {
    return sSchedulerSuspendCount.fetch_add(1);
}

s32 OSEnableScheduler(void) {
    return sSchedulerSuspendCount.fetch_sub(1);
}

// ============================================================================
// Interrupts (no-ops; see interrupt_lock.h)
// ============================================================================

BOOL OSDisableInterrupts(void) {
    return FALSE;
}

BOOL OSRestoreInterrupts(BOOL level) {
    return FALSE;
}

BOOL OSEnableInterrupts(void) {
    return FALSE;
}

// For SDK code that guards state shared with interrupt-context callbacks (the AXFX reverbs, RVLFaceLib):
// cmake/CfUnits.cmake builds it with OSDisableInterrupts / OSRestoreInterrupts renamed to these, which
// take the interrupt lock instead. Callers always pair them, so the lock is too.
BOOL CFDisableInterruptsLocked(void) {
    cf::sdk::InterruptLockAcquire();
    return TRUE;
}

BOOL CFRestoreInterruptsLocked(BOOL level) {
    (void)level;
    cf::sdk::InterruptLockRelease();
    return TRUE;
}

// ============================================================================
// Idle function (stub on PC)
// ============================================================================

OSThread* OSSetIdleFunction(OSIdleFunction idleFunction, void* param, void* stack, u32 stackSize) {
    return nullptr;
}

OSThread* OSGetIdleFunction(void) {
    return nullptr;
}

// ============================================================================
// Thread-specific storage
// ============================================================================

void OSSetThreadSpecific(s32 index, void* ptr) {
    OSThread* thread = OSGetCurrentThread();
    if (thread && index >= 0 && index < OS_THREAD_SPECIFIC_MAX) {
        thread->specific[index] = ptr;
    }
}

void* OSGetThreadSpecific(s32 index) {
    OSThread* thread = OSGetCurrentThread();
    if (thread && index >= 0 && index < OS_THREAD_SPECIFIC_MAX) {
        return thread->specific[index];
    }
    return nullptr;
}

// ============================================================================
// Clear stack (minimal implementation)
// ============================================================================

void OSClearStack(u8 val) {
    // On PC we don't clear the stack - it's managed by the OS
}

// ============================================================================
// Internal functions used by OSMutex
// ============================================================================

s32 __OSGetEffectivePriority(OSThread* thread) {
    // On PC with native threads, priority inversion handling is simplified.
    // Just return the base priority.
    return thread ? thread->base : 16;
}

void __OSPromoteThread(OSThread* thread, s32 priority) {
    // Simplified: no real priority inheritance on PC
    if (thread && priority < thread->priority) {
        thread->priority = priority;
    }
}

void __OSReschedule(void) {
    // With native OS threads, rescheduling is handled by the OS.
    // Nothing to do here.
}

// ============================================================================
// Interrupt handler registration (stub)
// ============================================================================

__OSInterruptHandler __OSSetInterruptHandler(__OSInterrupt interrupt,
                                             __OSInterruptHandler handler) {
    return nullptr;
}

OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask) {
    return 0;
}

void OSSetCurrentThreadName(const char* name) {
    // "Why is this current thread only?", you might ask?
    // Because macOS requires that. For some reason.

#if _WIN32
    wchar_t buffer[256];
    const auto converted = MultiByteToWideChar(
        CP_UTF8,
        0,
        name,
        -1,
        buffer,
        sizeof(buffer)/sizeof(wchar_t));
    if (converted == 0) {
        OSPanic(__FILE__, __LINE__, "OSSetThreadName: MultiByteToWideChar failed");
    }

    const auto result = SetThreadDescription(GetCurrentThread(), buffer);
    if (!SUCCEEDED(result)) {
        OSPanic(__FILE__, __LINE__, "OSSetThreadName: SetThreadDescription failed");
    }
#elif __APPLE__
    pthread_setname_np(name);
#endif
}

#ifdef __cplusplus
}
#endif
