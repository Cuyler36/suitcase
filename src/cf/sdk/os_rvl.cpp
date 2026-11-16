// RVL (Wii-only) OS functions that aurora does not provide (declared in src/cf/include/revolution/os.h
// and aurora's headers): MEM1/MEM2 arenas, alarms, sleeping, system exits and the REL linker.
#include <revolution/os.h>

#include <borealis/log_c.h>

#include "cf/version_config.h"
#include "shutdown.h"
#include "thread_system.h"

#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

// ---------------------------------------------------------------------------------------------
// MEM2 arena: a host block (the Wii's MEM2 is 64 MiB). MEM1 is aurora's arena.
// ---------------------------------------------------------------------------------------------
// 256 MiB rather than the Wii's 64: heap contents grow on 64-bit (EGG::Heap::pcContentSize)
constexpr size_t MEM2_SIZE = 256 * 1024 * 1024;

struct Mem2Arena {
    u8* base = nullptr;
    void* lo = nullptr;
    void* hi = nullptr;

    Mem2Arena() {
        base = static_cast<u8*>(std::calloc(MEM2_SIZE + 64, 1));
        u8* aligned = reinterpret_cast<u8*>((reinterpret_cast<uintptr_t>(base) + 63) & ~uintptr_t(63));
        lo = aligned;
        hi = aligned + MEM2_SIZE;
    }
};

Mem2Arena& GetMem2() {
    static Mem2Arena sArena;
    return sArena;
}

void* AllocFromLo(void*& lo, u32 size, u32 align) {
    uintptr_t p = (reinterpret_cast<uintptr_t>(lo) + align - 1) & ~uintptr_t(align - 1);
    lo = reinterpret_cast<void*>((p + size + align - 1) & ~uintptr_t(align - 1));
    return reinterpret_cast<void*>(p);
}

// ---------------------------------------------------------------------------------------------
// Alarms: one host thread fires due alarms in time order. On the Wii handlers run in interrupt
// context, so they run here with "interrupts" disabled (the PC thread layer's interrupt lock).
// ---------------------------------------------------------------------------------------------
class AlarmThread {
public:
    static AlarmThread& Get() {
        static AlarmThread sThread;
        return sThread;
    }

    void Set(OSAlarm* alarm, OSTime fire, OSTime period, OSAlarmHandler handler) {
        {
            std::lock_guard lock(mMutex);
            RemoveLocked(alarm);
            alarm->handler = handler;
            alarm->fire = fire;
            alarm->period = period;
            mAlarms.push_back(alarm);
        }
        mCv.notify_all();
    }

    void Cancel(OSAlarm* alarm) {
        std::lock_guard lock(mMutex);
        RemoveLocked(alarm);
        alarm->handler = nullptr;
    }

    // Ends the thread (shutdown.h); alarms set later do not fire
    void Stop() {
        {
            std::lock_guard lock(mMutex);
            mStop = true;
        }
        mCv.notify_all();
        if (mThread.joinable()) {
            mThread.join();
        }
    }

private:
    AlarmThread() : mThread([this] { Run(); }) {}

    void RemoveLocked(OSAlarm* alarm) {
        std::erase(mAlarms, alarm);
    }

    void Run() {
        std::unique_lock lock(mMutex);
        while (!mStop) {
            if (mAlarms.empty()) {
                mCv.wait(lock);
                continue;
            }
            OSAlarm* next = mAlarms.front();
            for (OSAlarm* alarm : mAlarms) {
                if (alarm->fire < next->fire) {
                    next = alarm;
                }
            }
            OSTime now = OSGetTime();
            if (next->fire > now) {
                auto wait = std::chrono::microseconds(OSTicksToMicroseconds(next->fire - now));
                mCv.wait_for(lock, wait);
                continue;
            }

            OSAlarmHandler handler = next->handler;
            if (next->period > 0) {
                next->fire += next->period;
            } else {
                RemoveLocked(next);
            }
            if (handler != nullptr) {
                lock.unlock();
                BOOL level = OSDisableInterrupts();
                handler(next, nullptr);
                OSRestoreInterrupts(level);
                lock.lock();
            }
        }
    }

    std::mutex mMutex;
    std::condition_variable mCv;
    std::vector<OSAlarm*> mAlarms;
    bool mStop = false;
    std::thread mThread; // last: started once the rest is constructed
};

// The RVL OSAlarm has a userData field; aurora's does not
std::mutex sUserDataMutex;
std::unordered_map<OSAlarm*, void*> sUserData;

OSPowerCallback sPowerCallback;
OSResetCallback sResetCallback;

// The host loop ends the game (thread_system.h RequestExit), shutting everything down in order
[[noreturn]] void ExitGame(const char* why) {
    borealis_log_printf(BOREALIS_LOG_INFO, "cf::os", "%s: exiting", why);
    cf::sdk::RequestExit();
}

} // namespace

namespace cf {
namespace sdk {
void ShutdownAlarms() {
    AlarmThread::Get().Stop();
}

// The whole MEM2 block, for the DSP address space (dsp_memory.cpp)
void GetMem2Range(u8** start, size_t* size) {
    *start = static_cast<u8*>(GetMem2().hi) - MEM2_SIZE;
    *size = MEM2_SIZE;
}
} // namespace sdk
} // namespace cf

extern "C" {

// MEM1 is aurora's arena (OSGetArenaLo/Hi)
void* OSGetMEM1ArenaHi(void) {
    return OSGetArenaHi();
}
void* OSGetMEM1ArenaLo(void) {
    return OSGetArenaLo();
}
void OSSetMEM1ArenaHi(void* hi) {
    OSSetArenaHi(hi);
}
void OSSetMEM1ArenaLo(void* lo) {
    OSSetArenaLo(lo);
}
void* OSAllocFromMEM1ArenaLo(u32 size, u32 align) {
    return OSAllocFromArenaLo(size, align);
}

void* OSGetMEM2ArenaHi(void) {
    return GetMem2().hi;
}
void* OSGetMEM2ArenaLo(void) {
    return GetMem2().lo;
}
void OSSetMEM2ArenaHi(void* hi) {
    GetMem2().hi = hi;
}
void OSSetMEM2ArenaLo(void* lo) {
    GetMem2().lo = lo;
}
void* OSAllocFromMEM2ArenaLo(u32 size, u32 align) {
    return AllocFromLo(GetMem2().lo, size, align);
}

void OSSleepTicks(OSTime ticks) {
    std::this_thread::sleep_for(std::chrono::microseconds(OSTicksToMicroseconds(ticks)));
    // A game thread polling with sleeps ends here when the game ends
    cf::sdk::ThreadCancelPoint();
}

void OSCreateAlarm(OSAlarm* alarm) {
    alarm->handler = nullptr;
    alarm->tag = 0;
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler) {
    AlarmThread::Get().Set(alarm, OSGetTime() + tick, 0, handler);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
    alarm->start = start;
    AlarmThread::Get().Set(alarm, start + period, period, handler);
}

void OSCancelAlarm(OSAlarm* alarm) {
    AlarmThread::Get().Cancel(alarm);
}

void OSSetAlarmUserData(OSAlarm* alarm, void* userData) {
    std::lock_guard lock(sUserDataMutex);
    sUserData[alarm] = userData;
}

void* OSGetAlarmUserData(OSAlarm* alarm) {
    std::lock_guard lock(sUserDataMutex);
    auto it = sUserData.find(alarm);
    return it != sUserData.end() ? it->second : nullptr;
}

// The 4-character game code of the disc (e.g. "RUUE")
const char* OSGetAppGamename(void) {
    static char sGamename[5];
    if (sGamename[0] == '\0') {
        std::snprintf(sGamename, sizeof(sGamename), "%.4s", CF_GAME_ID);
    }
    return sGamename;
}

u8 OSGetAppType(void) {
    return OS_APP_TYPE_DVD;
}

void OSRegisterVersion(const char* id) {
    borealis_log_printf(BOREALIS_LOG_DEBUG, "cf::os", "%s", id);
}

void OSPanic(const char* file, int line, const char* msg, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, msg);
    std::vsnprintf(buffer, sizeof(buffer), msg, args);
    va_end(args);
    borealis_log_printf(BOREALIS_LOG_FATAL, "cf::os", "[%s:%d] %s", file, line, buffer);
    std::abort();
}

// System exits: the PC build quits
void OSRebootSystem(void) {
    ExitGame("OSRebootSystem");
}
void OSRestart(u32 resetCode) {
    (void)resetCode;
    ExitGame("OSRestart");
}
void OSReturnToMenu(void) {
    ExitGame("OSReturnToMenu");
}
void OSReturnToDataManager(void) {
    ExitGame("OSReturnToDataManager");
}
void OSShutdownSystem(void) {
    ExitGame("OSShutdownSystem");
}

OSPowerCallback OSSetPowerCallback(OSPowerCallback callback) {
    OSPowerCallback old = sPowerCallback;
    sPowerCallback = callback;
    return old;
}

OSResetCallback OSSetResetCallback(OSResetCallback callback) {
    OSResetCallback old = sResetCallback;
    sResetCallback = callback;
    return old;
}

// RELs are linked statically on PC (plan 5.5); the module loader has nothing to do
void OSSetStringTable(void* stringTable) {
    (void)stringTable;
}
BOOL OSLink(OSModuleInfo* newModule, void* bss) {
    (void)newModule;
    (void)bss;
    return TRUE;
}
BOOL OSLinkFixed(OSModuleInfo* newModule, void* bss) {
    (void)newModule;
    (void)bss;
    return TRUE;
}
BOOL OSUnlink(OSModuleInfo* oldModule) {
    (void)oldModule;
    return TRUE;
}

void PPCSync(void) {}

} // extern "C"
