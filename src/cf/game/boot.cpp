// Starts the game in place of the Wii's main() (src/dol/game/d_main.cpp), which runs the game on its
// own thread and suspends the boot thread. The PC build runs it on the main thread, one frame per
// pass of the host loop (src/cf/main.cpp), as Dusklight does.
#include "boot.hpp"
#include "dol_data_loader.hpp"

#include <game/game/d_main.hpp>
#include <game/game/d_profile.hpp>
#include <game/game/d_sys.hpp>
#include <game/snd/snd_manager.hpp>

#include <nw4r/snd/snd_SoundSystem.h>
#include <nw4r/snd/snd_SoundThread.h>
#include <nw4r/snd/snd_TaskManager.h>

#include <revolution/ax.h>
#include <revolution/os.h>

#include <borealis/log_c.h>

#include "../sdk/shutdown.h"

#include <cf/save_codec.h>

#include <cstdlib>

namespace cf {
namespace {

// The game never shuts sound down (the Wii resets instead), and SoundManager's static instance would
// free its heaps in its destructor at exit, after nw4r's sound thread and singletons are gone. So the
// sound ends here while all of it exists: the heaps (their sounds are stopped as they go), the
// archive, nw4r snd's threads and managers, then AX's output.
void ShutdownSound() {
    if (!nw4r::snd::SoundSystem::IsInitializedSoundSystem()) {
        return;
    }
    SoundManager* manager = SoundManager::getInstance();
    // Effects off and voices stopped (the game's reset path)
    manager->shutdown();
    // No group load runs into a heap that is being freed
    nw4r::snd::detail::TaskManager::GetInstance().CancelAllTask();
    {
        nw4r::snd::detail::SoundThread::AutoLock lock;
        manager->mHeap2CC.Destroy();
        manager->mHeap2A0.Destroy();
        manager->mHeap2F8.Destroy();
        manager->mHeap324.Destroy();
        manager->mHeap.Destroy();
    }
    manager->mArchive.Close();
    nw4r::snd::SoundSystem::ShutdownSoundSystem();
    AXQuit();
}

} // namespace

bool BootGame() {
    // aurora's MEM1, arena, boot info and clock (the Wii's boot code does this before main)
    OSInit();

    // CF_SAVE_SELFTEST=<rvforest.dat>: check the save codec on a save file, then quit
    if (const char* selfTest = std::getenv("CF_SAVE_SELFTEST")) {
        save::SelfTest(selfTest);
        return false;
    }

    if (!LoadDolData()) {
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::boot", "the data from main.dol is missing; is this the right disc?");
        return false;
    }

    // On the Wii, d_profileNP's prolog installs the profile table when d_dylink links it. The RELs are
    // linked statically and REL linking is a no-op (src/dol/cLib/c_dylink.cpp), so install it here.
    dProfile::install();

    dMain::init();
    return true;
}

void RunGameFrame() {
    dSys::runFrame();
}

void ShutdownGame() {
    // In dependency order: sound (its threads use the SDK's), the SDK's worker threads (their
    // callbacks run game code), then the game's own threads
    ShutdownSound();
    sdk::ShutdownAlarms();
    sdk::ShutdownNandWorker();
    if (!sdk::StopGameThreads(std::chrono::seconds(2))) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::boot", "some game threads did not end");
    }
}

} // namespace cf
