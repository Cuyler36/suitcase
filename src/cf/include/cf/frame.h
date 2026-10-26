// The frame rate on PC (src/cf/game/frame.cpp).
//
// The game runs in ticks of 1/60 s: everything in it counts ticks, so the ticks stay at 60 Hz. At a
// higher frame rate the frames in between draw the last tick again (dSys::presentFrame) instead of
// running the game. The rate is graphics.frameRate: 60 is the game's (paced by the retrace wait,
// VIWaitForRetrace), higher is paced here, 0 is unlimited.
#pragma once

namespace cf {
namespace frame {

constexpr int kTickRate = 60;

void SetFrameRate(int rate);
int GetFrameRate();

// Whether frames are paced here, apart from the ticks (a frame rate other than 60)
bool IsDecoupled();

enum class Step {
    Tick,    // run a tick (the game's frame: draw, then logic)
    Present, // draw the last tick again
    Skip,    // nothing to draw: no frame
};

// Before each frame: waits for its time, and says what it does
Step NextStep(bool canPresent);

// Measured over the last half second: frames presented (ticks and frames between them) and ticks,
// per second, and the frames' average time in milliseconds
struct Stats {
    float framesPerSecond = 0;
    float ticksPerSecond = 0;
    float frameTimeMs = 0;
};
Stats GetStats();

} // namespace frame
} // namespace cf
