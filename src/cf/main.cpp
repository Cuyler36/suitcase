#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <borealis/aurora_log.h>
#include <borealis/log.hpp>
#include <dolphin/gx.h>

#include "cf/version_config.h"

namespace {
constexpr borealis::Log Log{"cf::main"};
}

int main(int argc, char* argv[]) {
    borealis::log::init({.filePrefix = "suitcase"});
    Log.info("suitcase {} (game ID {}, revision {})", CF_VERSION_NAME, CF_GAME_ID, CF_DISC_REVISION);

    const AuroraConfig config{
        .appName = "suitcase",
        .logCallback = borealis::log::aurora_callback(),
    };
    aurora_initialize(argc, argv, &config);

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
        GXSetCopyClear(GXColor{0x20, 0x40, 0x20, 0xFF}, GX_MAX_Z24);
        aurora_end_frame();
    }

    aurora_shutdown();
    borealis::log::shutdown();
    return 0;
}
