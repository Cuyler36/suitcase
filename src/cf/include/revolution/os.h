#ifndef REVOLUTION_OS_H
#define REVOLUTION_OS_H
// PC: aurora's OS, plus the RVL (Wii-only) parts of the decomp's include/lib/revolution/os.h.
// Functions without an aurora implementation are provided by src/cf/sdk.
#include <revolution/types.h>
#include <dolphin/os.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_CONSOLE_MASK (0xF000 << 16)
#define OS_CONSOLE_MASK_RVL (0x0000 << 16)
#define OS_CONSOLE_MASK_DEV (0x1000 << 16)
#define OS_CONSOLE_MASK_TDEV (0x2000 << 16)
#define OS_CONSOLE_RETAIL (OS_CONSOLE_MASK_RVL + 0x0021)

enum {
    OS_APP_TYPE_IPL = 0x40,
    OS_APP_TYPE_DVD = 0x80,
    OS_APP_TYPE_CHANNEL = 0x81,
    OS_APP_TYPE_84 = 0x84,
};

u8 OSGetAppType(void);
const char* OSGetAppGamename(void);

// MEM1/MEM2 arenas (OSArena.h)
void* OSGetMEM1ArenaHi(void);
void* OSGetMEM2ArenaHi(void);
void* OSGetMEM1ArenaLo(void);
void* OSGetMEM2ArenaLo(void);
void OSSetMEM1ArenaHi(void* hi);
void OSSetMEM2ArenaHi(void* hi);
void OSSetMEM1ArenaLo(void* lo);
void OSSetMEM2ArenaLo(void* lo);
void* OSAllocFromMEM1ArenaLo(u32 size, u32 align);
void* OSAllocFromMEM2ArenaLo(u32 size, u32 align);

// OSThread.h
typedef void* (*OSThreadFunc)(void* param);

// OSTime.h
void OSSleepTicks(OSTime ticks);
#define OSSleepSeconds(s) OSSleepTicks(OSSecondsToTicks((OSTime)s))
#define OSSleepMilliseconds(ms) OSSleepTicks(OSMillisecondsToTicks((OSTime)ms))
#define OSSleepMicroseconds(us) OSSleepTicks(OSMicrosecondsToTicks((OSTime)us))

// OSAlarm.h
void OSSetAlarmUserData(OSAlarm* alarm, void* userData);
void* OSGetAlarmUserData(OSAlarm* alarm);

// OSReset.h / OSResetSW.h
void OSRebootSystem(void);
void OSReturnToMenu(void);
void OSShutdownSystem(void);
void OSRestart(u32 resetCode);
void OSReturnToDataManager(void);
typedef void (*OSPowerCallback)(void);
OSPowerCallback OSSetPowerCallback(OSPowerCallback callback);

// OSFastCast.h: aurora's fast casts need no GQR setup
#define OS_GQR_F32 0x0000
#define OS_GQR_U8 0x0004
#define OS_GQR_U16 0x0005
#define OS_GQR_S8 0x0006
#define OS_GQR_S16 0x0007
static inline void OSSetGQR6(u32 type, u32 scale) {
    (void)type;
    (void)scale;
}
static inline void OSSetGQR7(u32 type, u32 scale) {
    (void)type;
    (void)scale;
}

#ifndef ASSERTLINE
#define ASSERTLINE(x, line) ((void)0)
#define ASSERTMSGLINE(x, line, ...) ((void)0)
#endif

// Physical addresses: aurora's functions only cover MEM1; these also cover MEM2 and any other host
// memory, keeping aurora's MEM1 mapping (src/cf/sdk/dsp_memory.cpp). The macros accept const
// pointers, as the RVL SDK's did.
u32 CFCachedToPhysical(const void* caddr);
void* CFPhysicalToCached(u32 paddr);
#define OSCachedToPhysical(caddr) CFCachedToPhysical(caddr)
#define OSUncachedToPhysical(ucaddr) CFCachedToPhysical(ucaddr)
#define OSPhysicalToCached(paddr) CFPhysicalToCached(paddr)
#define OSPhysicalToUncached(paddr) CFPhysicalToCached(paddr)

// OSError.h
#define OSHalt(msg, line) OSPanic(__FILE__, line, msg)
#define OSAssertMsg(exp, msg, line)     if (!(exp))                         OSHalt(msg, line)
#define OSAssertVMsg(exp, line, ...)     if (!(exp))                          OSPanic(__FILE__, line, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // REVOLUTION_OS_H
