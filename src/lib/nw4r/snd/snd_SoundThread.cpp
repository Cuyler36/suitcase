#include <nw4r/snd.h>
#include <nw4r/ut.h>

#include <revolution/os.h>

#if TARGET_PC
#include <atomic>
#endif

namespace nw4r {
namespace snd {
namespace detail {

#if TARGET_PC
namespace {
// AX frames the sound thread has not processed yet. On the Wii each comes from the DSP's 3 ms
// interrupt and the sound thread runs before the next, so its queue of 4 never fills. On PC the audio
// device asks for several frames at once (about 7 with PipeWire's default 21 ms), rendered back to
// back: one message per frame overflowed the queue, the dropped frames' sound updates were lost and
// the music played slow. One message stands for all the pending frames instead.
std::atomic<int> sPendingFrames(0);
} // namespace
#endif

SoundThread::SoundThread() : mStackEnd(NULL), mCreateFlag(false) {
    OSInitMessageQueue(&mMsgQueue, mMsgBuffer, MSG_QUEUE_CAPACITY);
    OSInitThreadQueue(&mThreadQueue);
    OSInitMutex(&mMutex);
}

SoundThread &SoundThread::GetInstance() {
    static SoundThread instance;
    return instance;
}

bool SoundThread::Create(s32 priority, void *pStack, u32 stackSize) {
    if (mCreateFlag) {
        return true;
    }

    mCreateFlag = true;
    mStackEnd = pStack;

    BOOL success = OSCreateThread(&mThread, SoundThreadFunc, &GetInstance(),
                                  static_cast<u8 *>(pStack) + stackSize,
                                  stackSize, priority, 0);

    if (success) {
        OSResumeThread(&mThread);
    }

    return success;
}

// Dead-stripped from CF's DOL, along with OSJamMessage, which CF never links. The PC calls it when the
// game ends (SoundSystem::ShutdownSoundSystem).
void SoundThread::Shutdown() {
    if (!mCreateFlag) {
        return;
    }

    OSJamMessage(&GetInstance().mMsgQueue,
                 reinterpret_cast<OSMessage>(MSG_SHUTDOWN), OS_MESSAGE_BLOCK);

    OSJoinThread(&mThread, NULL);
    mCreateFlag = false;
}

void SoundThread::AxCallbackFunc() {
    GetInstance().AxCallbackProc();
}

void SoundThread::AxCallbackProc() {
#if TARGET_PC
    if (sPendingFrames.fetch_add(1) == 0) {
        OSSendMessage(&mMsgQueue, reinterpret_cast<OSMessage>(MSG_AX_CALLBACK), 0);
    }
#else
    OSSendMessage(&mMsgQueue, reinterpret_cast<OSMessage>(MSG_AX_CALLBACK), 0);
#endif

    NW4R_UT_LINKLIST_FOREACH_SAFE(it, mPlayerCallbackList,
                                  { it->OnUpdateVoiceSoundThread(); })

    VoiceManager::GetInstance().NotifyVoiceUpdate();
}

void *SoundThread::SoundThreadFunc(void *pArg) {
    SoundThread *p = static_cast<SoundThread *>(pArg);

    AxManager::GetInstance().RegisterCallback(&p->mAxCallbackNode,
                                              AxCallbackFunc);

    p->SoundThreadProc();

    AxManager::GetInstance().UnregisterCallback(&p->mAxCallbackNode);

    return NULL;
}

void SoundThread::RegisterSoundFrameCallback(SoundFrameCallback *callback) {
    ut::detail::AutoLock<OSMutex> lock(mMutex);
    mSoundFrameCallbackList.PushBack(callback);
}

void SoundThread::UnregisterSoundFrameCallback(SoundFrameCallback *callback) {
    ut::detail::AutoLock<OSMutex> lock(mMutex);
    mSoundFrameCallbackList.Erase(callback);
}

void SoundThread::RegisterPlayerCallback(PlayerCallback *pCallback) {
    ut::detail::AutoLock<OSMutex> lock(mMutex);
    mPlayerCallbackList.PushBack(pCallback);
}

void SoundThread::UnregisterPlayerCallback(PlayerCallback *pCallback) {
    ut::detail::AutoLock<OSMutex> lock(mMutex);
    mPlayerCallbackList.Erase(pCallback);
}

void SoundThread::SoundThreadProc() {
    OSMessage msg;

    while (true) {
        OSReceiveMessage(&mMsgQueue, &msg, OS_MESSAGE_BLOCK);

        if (PTR_TO_U32(msg) == MSG_AX_CALLBACK) {
#if TARGET_PC
            // A sound frame for each pending AX frame (AxCallbackProc)
            do {
#endif
            ut::detail::AutoLock<OSMutex> lock(mMutex);

            NW4R_UT_LINKLIST_FOREACH_SAFE(it, mSoundFrameCallbackList,
                                          { it->OnBeginSoundFrame(); })

            u32 start = OSGetTick();
            {
                AxVoiceManager::GetInstance().FreeAllReservedAxVoice();
                AxManager::GetInstance().Update();

                NW4R_UT_LINKLIST_FOREACH_SAFE(it, mPlayerCallbackList, {
                    it->OnUpdateFrameSoundThread();
                })

                ChannelManager::GetInstance().UpdateAllChannel();

                (void)Util::CalcRandom();
                VoiceManager::GetInstance().UpdateAllVoices();
            }
            mProcessTick = OSGetTick() - start;

            NW4R_UT_LINKLIST_FOREACH_SAFE(it, mSoundFrameCallbackList,
                                          { it->OnEndSoundFrame(); })
#if TARGET_PC
            } while (sPendingFrames.fetch_sub(1) > 1);
#endif

        } else if (PTR_TO_U32(msg) == MSG_SHUTDOWN) {
            NW4R_UT_LINKLIST_FOREACH_SAFE(it, mPlayerCallbackList,
                                          { it->OnShutdownSoundThread(); })

            break;
        }
    }
}

} // namespace detail
} // namespace snd
} // namespace nw4r
