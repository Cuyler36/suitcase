// Ending the game on PC. The Wii never shuts its systems down (it resets), so the PC ends them in
// order (src/cf/game/boot.cpp ShutdownGame, then src/cf/main.cpp) before static destructors run:
// sound, the SDK's worker threads, the game threads, then the thread layer itself.
#pragma once

#include <chrono>

namespace cf {
namespace sdk {

// Thrown on the main thread by OSReturnToMenu and the other "leave the game" calls, so the frame
// unwinds to the host loop (src/cf/main.cpp), which ends the game normally
struct ExitRequested {};
// Whether a game thread (which cannot unwind the main thread) asked to leave the game
bool IsExitRequested();

// Stops the alarm thread (os_rvl.cpp); alarms set later do not fire
void ShutdownAlarms();
// Stops the NAND callback worker (nand_async.cpp); callbacks not yet run are dropped
void ShutdownNandWorker();

// Ends every game thread (OSCreateThread): threads blocked in an OS wait unwind and end, and the
// rest end at their next wait. Joins them; returns false (logging each) if some did not end within
// timeout, in which case DestroyThreadSystem keeps the state they may still use.
bool StopGameThreads(std::chrono::milliseconds timeout);

// Frees the thread layer's state, once nothing else can call the OS functions (after
// aurora_shutdown). The OS calls made after it (from static destructors) do not lock or wait.
void DestroyThreadSystem();

} // namespace sdk
} // namespace cf
