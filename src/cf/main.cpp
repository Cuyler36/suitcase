#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <borealis/aurora_log.h>
#include <borealis/log.hpp>
#include <dolphin/gx.h>
#include <cf/config.h>
#include <cf/debug.h>

#include <cstdlib>

#include "cf/version_config.h"
#if CF_BUILD_GAME
#include "game/boot.hpp"
#include "sdk/shutdown.h"
#endif

namespace {
constexpr borealis::Log Log{"cf::main"};

// Ends everything in order (src/cf/sdk/shutdown.h), so no thread is left for the static destructors
void Shutdown(bool dvdOpen) {
#if CF_BUILD_GAME
    cf::ShutdownGame();
    borealis::config::flush();
    if (dvdOpen) {
        aurora_dvd_close();
    }
#else
    (void)dvdOpen;
#endif
    aurora_shutdown();
#if CF_BUILD_GAME
    cf::sdk::DestroyThreadSystem();
#endif
    borealis::log::shutdown();
}
} // namespace

namespace cf::sdk {
void SetNandRoot(const char* userPath); // src/cf/sdk/nand_fs.cpp
}

// Usage: suitcase-<version> <disc image>
int main(int argc, char* argv[]) {
    borealis::log::init({.filePrefix = "suitcase"});
    Log.info("suitcase {} (game ID {}, revision {})", CF_VERSION_NAME, CF_GAME_ID, CF_DISC_REVISION);

    const AuroraConfig config{
        .appName = "suitcase",
        // Centred: with no position SDL put the client area at the screen's corner, so the title bar
        // was above the top edge and the window could not be moved
        .windowPosX = SDL_WINDOWPOS_CENTERED,
        .windowPosY = SDL_WINDOWPOS_CENTERED,
        .logCallback = borealis::log::aurora_callback(),
        // CF_GX_INLINE=1 processes GX commands on the game thread, so a GX failure has the game's stack
        .gxProcessingMode = std::getenv("CF_GX_INLINE") != nullptr ? GX_PROCESSING_INLINE : GX_PROCESSING_THREAD,
        // MEM1 is 24 MiB on the Wii. Heap contents grow on 64-bit (EGG::Heap::pcContentSize), and the
        // root heap gets the rest. MEM2 is the SDK layer's (src/cf/sdk/os_rvl.cpp).
        .mem1Size = 256 * 1024 * 1024,
    };
    const AuroraInfo info = aurora_initialize(argc, argv, &config);

#if CF_BUILD_GAME
    // Saves and the other NAND files live in the user directory (src/cf/sdk/nand_fs.cpp)
    cf::sdk::SetNandRoot(info.userPath);
    cf::config::Load(info.userPath);

    if (argc < 2) {
        Log.error("Usage: {} <disc image of {}>", argv[0], CF_GAME_ID);
        Shutdown(false);
        return 1;
    }
    if (!aurora_dvd_open(argv[1])) {
        Log.error("Could not open the disc image {}", argv[1]);
        Shutdown(false);
        return 1;
    }
    if (!cf::BootGame()) {
        Shutdown(true);
        return 1;
    }
#endif

    bool exiting = false;
    bool paused = false;
    while (!exiting) {
        for (const AuroraEvent* event = aurora_update(); event != nullptr && event->type != AURORA_NONE; ++event) {
            switch (event->type) {
            case AURORA_EXIT:
                exiting = true;
                break;
            case AURORA_PAUSED:
                paused = true;
                break;
            case AURORA_UNPAUSED:
                paused = false;
                break;
            default:
                break;
            }
        }
        if (exiting || paused || !aurora_begin_frame()) {
            continue;
        }
#if CF_BUILD_GAME
        try {
            cf::RunGameFrame();
            cf::debug::draw();
            // Saves the settings a moment after they change
            borealis::config::update();
        } catch (const cf::sdk::ExitRequested&) {
            // OSReturnToMenu and the like, on the main thread
            exiting = true;
        }
        if (cf::sdk::IsExitRequested()) {
            exiting = true;
        }
#else
        GXSetCopyClear(GXColor{0x20, 0x40, 0x20, 0xFF}, GX_MAX_Z24);
#endif
        aurora_end_frame();
    }

    Shutdown(true);
    return 0;
}
