// RVL SC (system settings), VI, GX and DVD functions that aurora does not provide, and the IPC
// registers the SDK headers declare.
#include <revolution/dvd.h>
#include <revolution/os.h>
#include <revolution/gx.h>
#include <revolution/sc.h>
#include <revolution/vi.h>

#include <aurora/wpad.h>

#include <cf/frame.h>
#include <cf/screen.h>

#include "thread_system.h"

#include <game_version.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

// Read from any thread (VIGetRetraceCount); counted by the main thread
std::atomic<u32> sRetraceCount{0};
VIRetraceCallback sPreRetraceCallback;
// The host main thread (static initialization runs on it): the game frame and aurora's event loop
const std::thread::id sMainThread = std::this_thread::get_id();
GXTexRegionCallback sTexRegionCallback;
OSThread* sCurrentGXThread;

} // namespace

extern "C" {

// ---------------------------------------------------------------------------------------------
// IPC: memory-mapped on the Wii (lib/revolution/IPC/ipcHardware.h); plain storage here
// ---------------------------------------------------------------------------------------------
volatile u32 IPC_HW_REGS_PPC[256];
volatile u32 IPC_HW_REGS[256];

// ---------------------------------------------------------------------------------------------
// SC: the console's settings, until the port has its own config (plan section 4)
// ---------------------------------------------------------------------------------------------
void SCInit(void) {}

u32 SCCheckStatus(void) {
    return SC_STATUS_OK;
}

u8 SCGetLanguage(void) {
#if VERSION == VERSION_RUUJ01_01 || VERSION == VERSION_RUUJ01_02
    return SC_LANG_JAPANESE;
#elif VERSION == VERSION_RUUK01_01
    return SC_LANG_KOREAN;
#else
    // PAL also starts in English until the language picker exists
    return SC_LANG_ENGLISH;
#endif
}

// The console's region (SC_AREA_*): the game's, as DWC reports it at its login
s8 SCGetProductArea(void) {
#if VERSION == VERSION_RUUJ01_01 || VERSION == VERSION_RUUJ01_02
    return 0; // Japan
#elif VERSION == VERSION_RUUK01_01
    return 4; // Korea
#elif VERSION == VERSION_RUUP01_00 || VERSION == VERSION_RUUP01_01
    return 2; // Europe
#else
    return 1; // USA
#endif
}

// The console's product code and serial: none (DWC then leaves the serial out of its login)
char* SCGetProductCode(void) {
    return nullptr;
}

BOOL SCGetProductSN(u32* sn) {
    (void)sn;
    return FALSE;
}

u8 SCGetSoundMode(void) {
    return SC_SOUND_MODE_STEREO;
}

u8 SCGetProgressiveMode(void) {
    return 0;
}

u8 SCGetEuRgb60Mode(void) {
    return 0;
}

// The game's widescreen mode, at the window's shape (cf/screen.h)
u8 SCGetAspectRatio(void) {
    return cf::screen::IsWidescreen() ? SC_ASPECT_RATIO_16x9 : SC_ASPECT_RATIO_4x3;
}

// Parental controls do not restrict messaging
BOOL SCCheckPCMessageRestriction(void) {
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// VI: frame pacing at 60 Hz until the game loop runs under aurora's frame loop
// ---------------------------------------------------------------------------------------------
void VIWaitForRetrace(void) {
    using Clock = std::chrono::steady_clock;
    constexpr auto period = std::chrono::nanoseconds(1000000000 / 60);
    if (std::this_thread::get_id() != sMainThread) {
        // Another thread (dNetDwc's session thread, EGG's DVD ripper) waits for a retrace: it sleeps for
        // one. The retrace itself (its count, the game's retrace callback, the Wii Remotes) is the main
        // thread's, as the VI interrupt ran it once per retrace. Run on both, the callback
        // (EGG::AsyncDisplay::postVRetrace) raced the main thread's frame, which then waited forever for a
        // frame buffer (pc_port_ub.md PC-35)
        std::this_thread::sleep_for(period);
        // A wait like the others: a game thread being ended (StopGameThreads) ends here. dNetDwc's session
        // thread loops on retraces alone and otherwise outlived the shutdown, into the static destructors
        cf::sdk::ThreadCancelPoint();
        return;
    }
    static Clock::time_point sNext = Clock::now() + period;
    auto now = Clock::now();
    if (cf::frame::IsSpeedup() || cf::frame::IsDecoupled()) {
        // The frame loop owns pacing at other frame rates; testing speedup removes pacing entirely.
        // Keep the deadline current so switching back never waits on an old schedule.
        sNext = now + period;
    } else if (now < sNext) {
        std::this_thread::sleep_until(sNext);
        sNext += period;
    } else {
        // Missed retraces are gone, as on the console: start counting again from now instead of
        // returning at once until a fixed schedule catches up (which ran the game unthrottled after
        // every stall, e.g. the title screen after loading)
        sNext = now + period;
    }
    const u32 count = ++sRetraceCount;
    // Samples the Wii Remotes; aurora's present-target branch does this from its input pump (aurora's
    // input router is the main thread's)
    aurora_wpad_update();
    if (sPreRetraceCallback != nullptr) {
        sPreRetraceCallback(count);
    }
}

u32 VIGetRetraceCount(void) {
    return sRetraceCount;
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback old = sPreRetraceCallback;
    sPreRetraceCallback = cb;
    return old;
}

void VISetBlack(BOOL black) {
    (void)black;
}

void VISetNextFrameBuffer(void* fb) {
    (void)fb;
}

u32 VIGetDTVStatus(void) {
    return 0;
}

// The frames left before the burn-in dimmer (reset by input). The PC doesn't dim the screen, so it
// never runs out: at 0 the game shows "Communications with the Wii Remote have been interrupted"
// (dObjTelop_c).
s32 VIGetDimmingCount(void) {
    return 0x7FFFFFFF;
}

// ---------------------------------------------------------------------------------------------
// GX: hardware management aurora handles itself
// ---------------------------------------------------------------------------------------------
void GXAbortFrame(void) {}

OSThread* GXSetCurrentGXThread(void) {
    OSThread* old = sCurrentGXThread;
    sCurrentGXThread = OSGetCurrentThread();
    return old;
}

OSThread* GXGetCurrentGXThread(void) {
    return sCurrentGXThread;
}

GXTexRegionCallback GXSetTexRegionCallback(GXTexRegionCallback f) {
    GXTexRegionCallback old = sTexRegionCallback;
    sTexRegionCallback = f;
    return old;
}

void GXSetZScaleOffset(f32 scale, f32 offset) {
    (void)scale;
    (void)offset;
}

void __GXSetIndirectMask(u32 mask) {
    (void)mask;
}

// ---------------------------------------------------------------------------------------------
// DVD: the disc is always present (aurora reads it through nod)
// ---------------------------------------------------------------------------------------------
BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCommandCallback callback) {
    if (callback != nullptr) {
        callback(1, block); // non-zero: the disc is there (dReset::Manage_c::DiskCheckCallback)
    }
    return TRUE;
}

} // extern "C"
