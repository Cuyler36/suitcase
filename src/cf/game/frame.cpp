// The frame rate on PC (src/cf/include/cf/frame.h).
#include <cf/frame.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace cf {
namespace frame {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kTickPeriod = std::chrono::nanoseconds(1000000000 / kTickRate);
// Ticks the schedule may fall behind before it starts again from now (a stall, e.g. loading): the
// game would otherwise run them back to back
constexpr int kMaxLateTicks = 4;

std::atomic<int> sFrameRate = kTickRate;
std::atomic<bool> sSpeedup = false;
Clock::time_point sNextTick;
Clock::time_point sNextFrame;
bool sStarted = false;

// The counts since the window started (GetStats)
constexpr auto kStatsWindow = std::chrono::milliseconds(500);
Clock::time_point sStatsStart = Clock::now();
int sFrames = 0;
int sTicks = 0;
Stats sStats;
float sTickFraction = 1.0f;

void Count(Step step) {
    if (step == Step::Skip) {
        return;
    }
    sFrames++;
    if (step == Step::Tick) {
        sTicks++;
    }
    const Clock::time_point now = Clock::now();
    const auto elapsed = now - sStatsStart;
    if (elapsed >= kStatsWindow) {
        const float seconds = std::chrono::duration<float>(elapsed).count();
        sStats.framesPerSecond = sFrames / seconds;
        sStats.ticksPerSecond = sTicks / seconds;
        sStats.frameTimeMs = sFrames > 0 ? seconds * 1000.0f / sFrames : 0.0f;
        sStatsStart = now;
        sFrames = 0;
        sTicks = 0;
    }
}

Step Decide(bool canPresent);

} // namespace

void SetFrameRate(int rate) {
    // Below the tick rate a frame would run at most one tick: the game would slow down
    sFrameRate = rate <= 0 ? 0 : std::max(rate, kTickRate);
}

int GetFrameRate() {
    return sFrameRate;
}

void SetSpeedup(bool enabled) {
    sSpeedup = enabled;
}

bool IsSpeedup() {
    return sSpeedup;
}

bool IsDecoupled() {
    return sFrameRate != kTickRate;
}

Step NextStep(bool canPresent) {
    const Step step = Decide(canPresent);
    Count(step);
    return step;
}

Stats GetStats() {
    return sStats;
}

float GetTickFraction() {
    return sTickFraction;
}

namespace {

Step Decide(bool canPresent) {
    if (IsSpeedup()) {
        // Advance gameplay on every frame, even with an interpolated presentation rate selected.
        // Restart the normal schedule when speedup ends; there is no elapsed-time debt to repay.
        sStarted = false;
        sTickFraction = 1.0f;
        return Step::Tick;
    }
    if (!IsDecoupled()) {
        // Every frame is a tick, paced by the retrace wait
        sStarted = false;
        return Step::Tick;
    }

    const int rate = sFrameRate;
    Clock::time_point now = Clock::now();
    if (!sStarted) {
        sStarted = true;
        sNextTick = now;
        sNextFrame = now;
    }

    // The frame's time, at most the next tick's: a tick is never late for a frame
    if (rate > 0) {
        const Clock::time_point due = std::min(sNextFrame, sNextTick);
        if (now < due) {
            std::this_thread::sleep_until(due);
            now = due;
        }
        const auto framePeriod = std::chrono::nanoseconds(1000000000 / rate);
        sNextFrame = std::max(sNextFrame + framePeriod, now);
    }

    if (now >= sNextTick) {
        sNextTick += kTickPeriod;
        if (now - sNextTick > kTickPeriod * kMaxLateTicks) {
            sNextTick = now + kTickPeriod;
        }
        return Step::Tick;
    }
    if (canPresent) {
        // sNextTick - kTickPeriod: the last tick's time
        const float fraction = std::chrono::duration<float>(now - (sNextTick - kTickPeriod)) /
                               std::chrono::duration<float>(kTickPeriod);
        sTickFraction = std::clamp(fraction, 0.0f, 1.0f);
        return Step::Present;
    }
    // Nothing to draw between ticks: wait for the next one
    std::this_thread::sleep_until(sNextTick);
    return Step::Skip;
}

} // namespace

} // namespace frame
} // namespace cf
