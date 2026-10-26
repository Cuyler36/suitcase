#pragma once

namespace cf {

// Sets the game up on the calling thread: the data taken from main.dol, the profile table and
// d_sys's init (heaps, video, the startup phases). Call after aurora_dvd_open. Returns false if the
// game cannot start.
bool BootGame();

// Runs one game frame (dSys::runFrame). Call between aurora_begin_frame and aurora_end_frame.
void RunGameFrame();

// Call before leaving main, booted or not, and before aurora_dvd_close: ends the sound, the SDK's
// worker threads and the game threads (src/cf/sdk/shutdown.h). Then, after aurora_shutdown,
// cf::sdk::DestroyThreadSystem.
void ShutdownGame();

} // namespace cf
