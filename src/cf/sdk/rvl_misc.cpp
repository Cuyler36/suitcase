// RVL SC (system settings), VI, GX and DVD functions that aurora does not provide, and the IPC
// registers the SDK headers declare.
#include <revolution/dvd.h>
#include <revolution/os.h>
#include <revolution/gx.h>
#include <revolution/sc.h>
#include <revolution/vi.h>

#include <aurora/wpad.h>

#include <cf/screen.h>

#include <game_version.h>

#include <chrono>
#include <thread>

namespace {

u32 sRetraceCount;
VIRetraceCallback sPreRetraceCallback;
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
    static Clock::time_point sNext = Clock::now() + period;
    auto now = Clock::now();
    if (now < sNext) {
        std::this_thread::sleep_until(sNext);
        sNext += period;
    } else {
        // Missed retraces are gone, as on the console: start counting again from now instead of
        // returning at once until a fixed schedule catches up (which ran the game unthrottled after
        // every stall, e.g. the title screen after loading)
        sNext = now + period;
    }
    sRetraceCount++;
    // Samples the Wii Remotes; aurora's present-target branch does this from its input pump
    aurora_wpad_update();
    if (sPreRetraceCallback != nullptr) {
        sPreRetraceCallback(sRetraceCount);
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
