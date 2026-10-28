// Starts the game in place of the Wii's main() (src/dol/game/d_main.cpp), which runs the game on its
// own thread and suspends the boot thread. The PC build runs it on the main thread, one frame per
// pass of the host loop (src/cf/main.cpp), as Dusklight does.
#include "boot.hpp"
#include "dol_data_loader.hpp"

#include <game/game/d_main.hpp>
#include <game/game/d_net_dwc.hpp>
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

#include <cf/debug.h>
#include <cf/save_codec.h>
#include <cf/frame.h>
#include <cf/screen.h>
#include <suitcase/wpad_host.hpp>

#include <chrono>
#include <cstdlib>
#include <thread>

extern "C" void aurora_wpad_update(void); // aurora/wpad.h

namespace cf {
namespace {

// Leaves an online session as the game does (dNetDwc::Session_c::requestStop): its thread closes the
// connections, which tells the other consoles (GT2's closed message), and logs out. Quitting without it
// left the Wiis to find out by timing out, mid scene change or demo. The thread only stops once no friend
// update is pending, so it gets a few seconds.
void StopOnlineSession() {
    dNetDwc::Session_c *session = dNetDwc::Session_c::sInstance;
    if (session == nullptr || OSIsThreadTerminated(&session->mThread)) {
        return;
    }
    session->requestStop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!OSIsThreadTerminated(&session->mThread) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!OSIsThreadTerminated(&session->mThread)) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::boot", "the online session did not stop in time");
    }
}

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
    // The window's shape for this frame (widescreen)
    screen::Update();
    debug::applyPending();
    // Samples the Wii Remotes once a tick, on the main thread (aurora's input router is its own), right
    // before the game reads them. aurora's input pump samples too, once a host frame; sampling goes by
    // elapsed time, so a second call in the same tick adds no samples. A newly connected gamepad gets
    // its Classic Controller profile first (src/cf/input/host_input.cpp).
    suitcase::wpad::update();
    aurora_wpad_update();
    dSys::runFrame();
}

bool CanPresentGameFrame() {
    return dSys::canPresent() != FALSE;
}

void PresentGameFrame() {
    screen::Update();
    dSys::presentFrame(frame::GetTickFraction());
}

void ShutdownGame() {
    // In dependency order: the online session (its thread uses sockets, alarms and the game's net
    // state), sound (its threads use the SDK's), the SDK's worker threads (their callbacks run game
    // code), then the game's own threads
    StopOnlineSession();
    ShutdownSound();
    sdk::ShutdownAlarms();
    sdk::ShutdownNandWorker();
    if (!sdk::StopGameThreads(std::chrono::seconds(2))) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::boot", "some game threads did not end");
    }
}

} // namespace cf
