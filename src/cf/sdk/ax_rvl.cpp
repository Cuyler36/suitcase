// Software AX: the Wii's audio DSP library, mixed on the CPU. aurora has no audio, so this implements
// the AX/AI calls the game makes (nw4r snd and SoundManager):
//
// - Voices: AXAcquireVoice/AXFreeVoice hand out AX_VOICE_MAX parameter blocks with the SDK's priority
//   stealing (the stolen voice's callback runs first). Game code writes vpb->pb and sets vpb->sync,
//   as with the SDK; each frame the synced fields are copied to the voice's DSP copy, the DSP copy is
//   rendered, and the state and current address (what the game polls) are copied back.
// - A frame is AX_SAMPLES_PER_FRAME (96) samples at 32 kHz, 3 ms. Per voice: DSP-ADPCM / PCM16
//   (big-endian) / PCM8 decoding with loops, sample rate conversion, the low-pass and biquad
//   filters, the volume envelope, and the mix into the main and aux A/B/C buses (L, R, S). The aux
//   buses go through the registered aux callbacks (nw4r's effects) and back into main at their
//   return volumes; then the master volume.
// - After rendering, the frame callback (AXRegisterCallback: nw4r's sound thread tick) and the AI
//   DMA callback run, all under the interrupt lock (interrupt_lock.h), as AX runs in interrupt
//   context on the Wii.
// - Sample addresses are the SDK's DSP addresses: physical byte addresses (dsp_memory.cpp) in
//   samples (PCM16), bytes (PCM8) or nibbles (ADPCM).
//
// Output goes to an SDL3 audio stream (32 kHz stereo; SDL converts to the device), whose callback
// renders frames as the device needs them, so the device clock paces AX as the DSP's did. Without
// an audio device, a timer thread runs the frames and drops the output. Not implemented: the remote
// (Wii Remote speaker) mix, ITD, depop, surround/DPL2 output (S is dropped in stereo).
#include <dolphin/ai.h>
#include <revolution/ax.h>
#include <revolution/os.h>

#include <borealis/log_c.h>

#include "interrupt_lock.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace {

constexpr int FRAME_SAMPLES = AX_SAMPLES_PER_FRAME;
constexpr int AUX_BUS_NUM = 3;

AXVPB sVoices[AX_VOICE_MAX];
AXPB sDsp[AX_VOICE_MAX]; // the DSP's copy of each voice's parameter block

bool sInitialized = false;
// The output: the audio device's stream, or the thread that runs the frames without one (AXQuit)
SDL_AudioStream* sStream = nullptr;
std::thread sFrameThread;
std::atomic<bool> sFrameThreadStop{false};
AXOutCallback sFrameCallback = nullptr;
AIDCallback sAiDmaCallback = nullptr;
AXAuxCallback sAuxCallback[AUX_BUS_NUM] = {};
void* sAuxContext[AUX_BUS_NUM] = {};
u16 sAuxReturnVolume[AUX_BUS_NUM] = {AX_MAX_VOLUME, AX_MAX_VOLUME, AX_MAX_VOLUME};
u16 sMasterVolume = AX_MAX_VOLUME;
u32 sMode = AX_OUTPUT_STEREO;

// Bus buffers: main L/R/S, then L/R/S of aux A, B and C
s32 sMain[AX_STEREO_MAX][FRAME_SAMPLES];
s32 sAux[AUX_BUS_NUM][AX_STEREO_MAX][FRAME_SAMPLES];

u32 Join(u16 hi, u16 lo) {
    return (static_cast<u32>(hi) << 16) | lo;
}

void Split(u32 value, u16* hi, u16* lo) {
    *hi = static_cast<u16>(value >> 16);
    *lo = static_cast<u16>(value & 0xFFFF);
}

s16 Clamp16(s32 value) {
    return static_cast<s16>(std::clamp(value, -32768, 32767));
}

// ---------------------------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------------------------
void ResetVoice(AXVPB* vpb) {
    std::memset(&vpb->pb, 0, sizeof(vpb->pb));
    vpb->pb.state = AX_VOICE_STOP;
    vpb->pb.srcSelect = 1; // linear
    vpb->sync = AX_PBSYNC_ALL;
}

// Copies the fields the game synced since the last frame to the DSP copy
void ApplySync(AXVPB& vpb, AXPB& dsp) {
    u32 sync = vpb.sync;
    vpb.sync = 0;
    const AXPB& pb = vpb.pb;
    if (sync & AX_PBSYNC_ALL) {
        dsp = pb;
        return;
    }
    if (sync & AX_PBSYNC_SELECT) {
        dsp.srcSelect = pb.srcSelect;
        dsp.coefSelect = pb.coefSelect;
    }
    if (sync & AX_PBSYNC_MIXER_CTRL) {
        dsp.mixerCtrl = pb.mixerCtrl;
    }
    if (sync & AX_PBSYNC_STATE) {
        dsp.state = pb.state;
    }
    if (sync & AX_PBSYNC_TYPE) {
        dsp.type = pb.type;
    }
    if (sync & AX_PBSYNC_MIX) {
        dsp.mix = pb.mix;
    }
    if (sync & AX_PBSYNC_ITD) {
        dsp.itd = pb.itd;
    }
    if (sync & AX_PBSYNC_DPOP) {
        dsp.dpop = pb.dpop;
    }
    if (sync & AX_PBSYNC_VE) {
        dsp.ve = pb.ve;
    } else if (sync & AX_PBSYNC_VE_DELTA) {
        dsp.ve.currentDelta = pb.ve.currentDelta;
    }
    if (sync & AX_PBSYNC_ADDR) {
        dsp.addr = pb.addr;
    } else {
        if (sync & AX_PBSYNC_LOOP_FLAG) {
            dsp.addr.loopFlag = pb.addr.loopFlag;
        }
        if (sync & AX_PBSYNC_LOOP_ADDR) {
            dsp.addr.loopAddressHi = pb.addr.loopAddressHi;
            dsp.addr.loopAddressLo = pb.addr.loopAddressLo;
        }
        if (sync & AX_PBSYNC_END_ADDR) {
            dsp.addr.endAddressHi = pb.addr.endAddressHi;
            dsp.addr.endAddressLo = pb.addr.endAddressLo;
        }
        if (sync & AX_PBSYNC_CURR_ADDR) {
            dsp.addr.currentAddressHi = pb.addr.currentAddressHi;
            dsp.addr.currentAddressLo = pb.addr.currentAddressLo;
        }
    }
    if (sync & AX_PBSYNC_ADPCM) {
        dsp.adpcm = pb.adpcm;
    }
    if (sync & AX_PBSYNC_SRC) {
        dsp.src = pb.src;
    } else if (sync & AX_PBSYNC_SRC_RATIO) {
        dsp.src.ratioHi = pb.src.ratioHi;
        dsp.src.ratioLo = pb.src.ratioLo;
    }
    if (sync & AX_PBSYNC_ADPCM_LOOP) {
        dsp.adpcmLoop = pb.adpcmLoop;
    }
    if (sync & AX_PBSYNC_LPF) {
        dsp.lpf = pb.lpf;
    } else if (sync & AX_PBSYNC_LPF_COEFS) {
        dsp.lpf.a0 = pb.lpf.a0;
        dsp.lpf.b0 = pb.lpf.b0;
    }
    if (sync & AX_PBSYNC_BIQUAD) {
        dsp.biquad = pb.biquad;
    } else if (sync & AX_PBSYNC_BIQUAD_COEFS) {
        dsp.biquad.b0 = pb.biquad.b0;
        dsp.biquad.b1 = pb.biquad.b1;
        dsp.biquad.b2 = pb.biquad.b2;
        dsp.biquad.a1 = pb.biquad.a1;
        dsp.biquad.a2 = pb.biquad.a2;
    }
    if (sync & AX_PBSYNC_REMOTE) {
        dsp.remote = pb.remote;
    }
    if (sync & AX_PBSYNC_RMT_MIXER_CTRL) {
        dsp.rmtMixerCtrl = pb.rmtMixerCtrl;
    }
    if (sync & AX_PBSYNC_RMTMIX) {
        dsp.rmtMix = pb.rmtMix;
    }
    if (sync & AX_PBSYNC_RMTIIR) {
        dsp.rmtIIR = pb.rmtIIR;
    }
}

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------
const u8* SampleBytes(u32 byteAddr) {
    return static_cast<const u8*>(CFPhysicalToCached(byteAddr));
}

// Reads the sample at the current address and moves on (to the loop start or stopping at the end)
class SampleReader {
public:
    explicit SampleReader(AXPB& pb)
        : mPb(pb), mCurrent(Join(pb.addr.currentAddressHi, pb.addr.currentAddressLo)),
          mEnd(Join(pb.addr.endAddressHi, pb.addr.endAddressLo)),
          mLoop(Join(pb.addr.loopAddressHi, pb.addr.loopAddressLo)) {}

    s16 Next() {
        if (mStopped) {
            return 0;
        }
        s16 sample;
        switch (mPb.addr.format) {
        case AX_SAMPLE_FORMAT_DSP_ADPCM:
            sample = NextAdpcm();
            break;
        case AX_SAMPLE_FORMAT_PCM_S16: {
            const u8* p = SampleBytes(mCurrent * 2);
            sample = p != nullptr ? static_cast<s16>((p[0] << 8) | p[1]) : 0;
            break;
        }
        case AX_SAMPLE_FORMAT_PCM_S8: {
            const u8* p = SampleBytes(mCurrent);
            sample = p != nullptr ? static_cast<s16>(static_cast<s8>(p[0]) << 8) : 0;
            break;
        }
        default:
            sample = 0;
            break;
        }
        Advance();
        return sample;
    }

    void Store() {
        Split(mCurrent, &mPb.addr.currentAddressHi, &mPb.addr.currentAddressLo);
    }

private:
    s16 NextAdpcm() {
        AXPBADPCM& adpcm = mPb.adpcm;
        if ((mCurrent & 15) == 0) {
            // A frame header: the predictor and scale of the next 14 samples
            const u8* header = SampleBytes(mCurrent >> 1);
            adpcm.pred_scale = header != nullptr ? *header : 0;
            mCurrent += 2;
        }
        const u8* p = SampleBytes(mCurrent >> 1);
        if (p == nullptr) {
            return 0;
        }
        int nibble = (mCurrent & 1) ? (*p & 0xF) : (*p >> 4);
        if (nibble >= 8) {
            nibble -= 16;
        }
        int index = (adpcm.pred_scale >> 4) & 7;
        int scale = 1 << (adpcm.pred_scale & 0xF);
        s32 coef1 = static_cast<s16>(adpcm.a[index][0]);
        s32 coef2 = static_cast<s16>(adpcm.a[index][1]);
        s32 yn1 = static_cast<s16>(adpcm.yn1);
        s32 yn2 = static_cast<s16>(adpcm.yn2);
        s32 value = ((nibble * scale) << 11) + 1024 + coef1 * yn1 + coef2 * yn2;
        s16 sample = Clamp16(value >> 11);
        adpcm.yn2 = static_cast<u16>(yn1);
        adpcm.yn1 = static_cast<u16>(sample);
        return sample;
    }

    void Advance() {
        if (mCurrent != mEnd) {
            mCurrent++;
            return;
        }
        // At the end the DSP goes to the loop start even when not looping, then stops: nw4r ends
        // sounds by pointing the loop start at its zero buffer and waits for the current address
        // to land there (AxVoice::IsPlayFinished)
        mCurrent = mLoop;
        if (!mPb.addr.loopFlag) {
            mStopped = true;
            mPb.state = AX_VOICE_STOP;
            return;
        }
        if (mPb.addr.format == AX_SAMPLE_FORMAT_DSP_ADPCM) {
            mPb.adpcm.pred_scale = mPb.adpcmLoop.loop_pred_scale;
            // A stream's loop continues the data written behind it, so its history carries on
            if (mPb.type != AX_VOICE_STREAM) {
                mPb.adpcm.yn1 = mPb.adpcmLoop.loop_yn1;
                mPb.adpcm.yn2 = mPb.adpcmLoop.loop_yn2;
            }
        }
    }

    AXPB& mPb;
    u32 mCurrent;
    u32 mEnd;
    u32 mLoop;
    bool mStopped = false;
};

// Catmull-Rom between h1 and h2 (t in [0, 1))
s32 Cubic(s32 h0, s32 h1, s32 h2, s32 h3, f32 t) {
    f32 a = -0.5f * h0 + 1.5f * h1 - 1.5f * h2 + 0.5f * h3;
    f32 b = h0 - 2.5f * h1 + 2.0f * h2 - 0.5f * h3;
    f32 c = -0.5f * h0 + 0.5f * h2;
    return static_cast<s32>(((a * t + b) * t + c) * t + h1);
}

// Decodes, resamples, filters and applies the volume envelope: one frame of the voice
void RenderVoice(AXPB& pb, s32 out[FRAME_SAMPLES]) {
    SampleReader reader(pb);
    u32 ratio = Join(pb.src.ratioHi, pb.src.ratioLo); // 16.16
    u32 frac = pb.src.currentAddressFrac;
    s32 h[4];
    for (int i = 0; i < 4; i++) {
        h[i] = static_cast<s16>(pb.src.last_samples[i]);
    }

    for (int i = 0; i < FRAME_SAMPLES; i++) {
        f32 t = frac / 65536.0f;
        s32 s;
        if (pb.srcSelect == 0) {
            s = Cubic(h[0], h[1], h[2], h[3], t);
        } else {
            s = h[1] + static_cast<s32>((h[2] - h[1]) * t);
        }
        out[i] = Clamp16(s);

        frac += ratio;
        u32 advance = std::min<u32>(frac >> 16, 0x10000);
        frac &= 0xFFFF;
        for (u32 n = 0; n < advance; n++) {
            h[0] = h[1];
            h[1] = h[2];
            h[2] = h[3];
            h[3] = reader.Next();
        }
    }

    reader.Store();
    pb.src.currentAddressFrac = static_cast<u16>(frac);
    for (int i = 0; i < 4; i++) {
        pb.src.last_samples[i] = static_cast<u16>(h[i]);
    }

    if (pb.lpf.on == AX_PB_LPF_ON) {
        s32 a0 = pb.lpf.a0;
        s32 b0 = pb.lpf.b0;
        s32 yn1 = static_cast<s16>(pb.lpf.yn1);
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            yn1 = Clamp16((a0 * out[i] + b0 * yn1) >> 15);
            out[i] = yn1;
        }
        pb.lpf.yn1 = static_cast<u16>(yn1);
    }

    if (pb.biquad.on == AX_PB_BIQUAD_ON) {
        AXPBBIQUAD& bq = pb.biquad;
        s32 b0 = static_cast<s16>(bq.b0), b1 = static_cast<s16>(bq.b1), b2 = static_cast<s16>(bq.b2);
        s32 a1 = static_cast<s16>(bq.a1), a2 = static_cast<s16>(bq.a2);
        s32 xn1 = static_cast<s16>(bq.xn1), xn2 = static_cast<s16>(bq.xn2);
        s32 yn1 = static_cast<s16>(bq.yn1), yn2 = static_cast<s16>(bq.yn2);
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            s32 x = out[i];
            s32 y = Clamp16((b0 * x + b1 * xn1 + b2 * xn2 + a1 * yn1 + a2 * yn2) >> 14);
            xn2 = xn1;
            xn1 = x;
            yn2 = yn1;
            yn1 = y;
            out[i] = y;
        }
        bq.xn1 = static_cast<u16>(xn1);
        bq.xn2 = static_cast<u16>(xn2);
        bq.yn1 = static_cast<u16>(yn1);
        bq.yn2 = static_cast<u16>(yn2);
    }

    s32 volume = pb.ve.currentVolume;
    s32 delta = static_cast<s16>(pb.ve.currentDelta);
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        out[i] = (out[i] * volume) >> 15;
        volume = std::clamp(volume + delta, 0, 0x7FFF);
    }
    pb.ve.currentVolume = static_cast<u16>(volume);
}

// AXPBMIX holds (volume, delta) pairs in this order; each goes to a bus channel
struct MixTarget {
    s32* bus;
    u32 deltaFlag;
};

void MixVoice(AXPB& pb, const s32 in[FRAME_SAMPLES]) {
    const MixTarget targets[12] = {
        {sMain[AX_STEREO_L], AX_MIXER_CTRL_DELTA},     {sMain[AX_STEREO_R], AX_MIXER_CTRL_DELTA},
        {sAux[0][AX_STEREO_L], AX_MIXER_CTRL_DELTA_A}, {sAux[0][AX_STEREO_R], AX_MIXER_CTRL_DELTA_A},
        {sAux[1][AX_STEREO_L], AX_MIXER_CTRL_DELTA_B}, {sAux[1][AX_STEREO_R], AX_MIXER_CTRL_DELTA_B},
        {sAux[2][AX_STEREO_L], AX_MIXER_CTRL_DELTA_C}, {sAux[2][AX_STEREO_R], AX_MIXER_CTRL_DELTA_C},
        {sMain[AX_STEREO_S], AX_MIXER_CTRL_DELTA_S},   {sAux[0][AX_STEREO_S], AX_MIXER_CTRL_DELTA_AS},
        {sAux[1][AX_STEREO_S], AX_MIXER_CTRL_DELTA_BS}, {sAux[2][AX_STEREO_S], AX_MIXER_CTRL_DELTA_CS},
    };
    u16* mix = reinterpret_cast<u16*>(&pb.mix);
    for (int c = 0; c < 12; c++) {
        s32 volume = mix[c * 2];
        s32 delta = (pb.mixerCtrl & targets[c].deltaFlag) ? static_cast<s16>(mix[c * 2 + 1]) : 0;
        if (volume == 0 && delta == 0) {
            continue;
        }
        s32* bus = targets[c].bus;
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            bus[i] += (in[i] * volume) >> 15;
            volume = std::clamp(volume + delta, 0, 0xFFFF);
        }
        mix[c * 2] = static_cast<u16>(volume);
    }
}

// One AX frame; out (FRAME_SAMPLES interleaved stereo samples) may be null
void RunFrame(s16* out) {
    cf::sdk::InterruptLockScope lock;

    std::memset(sMain, 0, sizeof(sMain));
    std::memset(sAux, 0, sizeof(sAux));

    s32 voiceOut[FRAME_SAMPLES];
    for (int v = 0; v < AX_VOICE_MAX; v++) {
        AXVPB& vpb = sVoices[v];
        if (vpb.priority == AX_PRIORITY_FREE) {
            continue;
        }
        AXPB& dsp = sDsp[v];
        ApplySync(vpb, dsp);
        if (dsp.state == AX_VOICE_RUN) {
            RenderVoice(dsp, voiceOut);
            MixVoice(dsp, voiceOut);
        }
        // What the game polls: whether the voice still runs, and where it is
        vpb.pb.state = dsp.state;
        vpb.pb.addr.currentAddressHi = dsp.addr.currentAddressHi;
        vpb.pb.addr.currentAddressLo = dsp.addr.currentAddressLo;
    }

    for (int b = 0; b < AUX_BUS_NUM; b++) {
        if (sAuxCallback[b] == nullptr) {
            continue;
        }
        void* chans[AX_STEREO_MAX] = {sAux[b][AX_STEREO_L], sAux[b][AX_STEREO_R], sAux[b][AX_STEREO_S]};
        sAuxCallback[b](chans, sAuxContext[b]);
        s32 volume = sAuxReturnVolume[b];
        for (int ch = 0; ch < AX_STEREO_MAX; ch++) {
            for (int i = 0; i < FRAME_SAMPLES; i++) {
                sMain[ch][i] += (sAux[b][ch][i] * volume) >> 15;
            }
        }
    }

    if (out != nullptr) {
        s32 master = sMasterVolume;
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            out[i * 2 + 0] = Clamp16((sMain[AX_STEREO_L][i] * master) >> 15);
            out[i * 2 + 1] = Clamp16((sMain[AX_STEREO_R][i] * master) >> 15);
        }
    }

    if (sFrameCallback != nullptr) {
        sFrameCallback();
    }
    if (sAiDmaCallback != nullptr) {
        sAiDmaCallback();
    }
}

// AX's own AI DMA handler, which AXInit installs on the Wii (the DSP's frame comes from it). nw4r's
// AxManager::PrepareReset chains to the previous callback without checking it.
void AxDmaCallback() {}

// The audio device wants more: render whole frames until it has at least that much
void SDLCALL AudioStreamCallback(void* userdata, SDL_AudioStream* stream, int additional, int total) {
    (void)userdata;
    (void)total;
    s16 frame[FRAME_SAMPLES * 2];
    while (additional > 0) {
        RunFrame(frame);
        SDL_PutAudioStreamData(stream, frame, sizeof(frame));
        additional -= static_cast<int>(sizeof(frame));
    }
}

bool OpenAudioDevice() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::ax", "SDL audio: %s", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AX_SAMPLE_RATE};
    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioStreamCallback, nullptr);
    if (stream == nullptr) {
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::ax", "no audio device: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    sStream = stream;
    return true;
}

// Without an audio device: runs the frames in real time (3 ms each), catching up after short stalls
// and starting over after long ones, and drops the output
void FrameThread() {
    using Clock = std::chrono::steady_clock;
    constexpr auto period = std::chrono::nanoseconds(1000000000LL * FRAME_SAMPLES / AX_SAMPLE_RATE);
    Clock::time_point next = Clock::now();
    while (!sFrameThreadStop) {
        Clock::time_point now = Clock::now();
        if (now - next > std::chrono::milliseconds(100)) {
            next = now;
        }
        while (next <= now) {
            RunFrame(nullptr);
            next += period;
        }
        std::this_thread::sleep_until(next);
    }
}

} // namespace

extern "C" {

// ---------------------------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------------------------
void AIInit(u8* stack) {
    (void)stack;
}

void AIInitDMA(uintptr_t start_addr, u32 length) {
    (void)start_addr;
    (void)length;
}

u32 AIGetDMAStartAddr(void) {
    return 0;
}

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    cf::sdk::InterruptLockScope lock;
    AIDCallback old = sAiDmaCallback;
    sAiDmaCallback = callback;
    return old;
}

// ---------------------------------------------------------------------------------------------
// AX
// ---------------------------------------------------------------------------------------------
void AXInit() {
    AXInitEx(0);
}

void AXInitEx(u32 outputBufferMode) {
    (void)outputBufferMode;
    {
        cf::sdk::InterruptLockScope lock;
        if (sInitialized) {
            return;
        }
        sInitialized = true;
        for (int i = 0; i < AX_VOICE_MAX; i++) {
            AXVPB& vpb = sVoices[i];
            std::memset(&vpb, 0, sizeof(vpb));
            vpb.index = i;
            vpb.priority = AX_PRIORITY_FREE;
            ResetVoice(&vpb);
            std::memset(&sDsp[i], 0, sizeof(sDsp[i]));
        }
        sAiDmaCallback = AxDmaCallback;
    }
    if (!OpenAudioDevice()) {
        sFrameThread = std::thread(FrameThread);
    }
}

// Stops the output, then the voices and callbacks. The PC calls it when the game ends (the SDK's
// AXQuit; src/cf/game/boot.cpp), after nw4r snd has shut down.
void AXQuit() {
    // Destroying the stream waits for a running device callback (which renders under the interrupt
    // lock), so the lock is not held here
    if (sStream != nullptr) {
        SDL_DestroyAudioStream(sStream);
        sStream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    if (sFrameThread.joinable()) {
        sFrameThreadStop = true;
        sFrameThread.join();
        sFrameThreadStop = false;
    }

    cf::sdk::InterruptLockScope lock;
    sInitialized = false;
    sFrameCallback = nullptr;
    sAiDmaCallback = nullptr;
    for (int b = 0; b < AUX_BUS_NUM; b++) {
        sAuxCallback[b] = nullptr;
        sAuxContext[b] = nullptr;
    }
    for (AXVPB& vpb : sVoices) {
        vpb.priority = AX_PRIORITY_FREE;
        vpb.callback = nullptr;
    }
}

AXVPB* AXAcquireVoice(u32 priority, AXVoiceCallback callback, AXUserContext userContext) {
    cf::sdk::InterruptLockScope lock;
    AXVPB* vpb = nullptr;
    for (AXVPB& v : sVoices) {
        if (v.priority == AX_PRIORITY_FREE) {
            vpb = &v;
            break;
        }
    }
    if (vpb == nullptr) {
        // Take the lowest-priority voice below the request; its owner hears about it first
        for (AXVPB& v : sVoices) {
            if (v.priority < priority && (vpb == nullptr || v.priority < vpb->priority)) {
                vpb = &v;
            }
        }
        if (vpb == nullptr) {
            return nullptr;
        }
        if (vpb->callback != nullptr) {
            vpb->callback(vpb);
        }
    }
    vpb->priority = priority;
    vpb->callback = callback;
    vpb->userContext = userContext;
    ResetVoice(vpb);
    return vpb;
}

void AXFreeVoice(AXVPB* p) {
    cf::sdk::InterruptLockScope lock;
    p->priority = AX_PRIORITY_FREE;
    p->callback = nullptr;
    p->pb.state = AX_VOICE_STOP;
    p->sync |= AX_PBSYNC_STATE;
    sDsp[p->index].state = AX_VOICE_STOP;
}

void AXSetVoicePriority(AXVPB* p, u32 priority) {
    cf::sdk::InterruptLockScope lock;
    p->priority = priority;
}

s32 AXGetMaxVoices(void) {
    return AX_VOICE_MAX;
}

void AXSetMaxDspCycles(u32 num) {
    (void)num;
}

AXOutCallback AXRegisterCallback(AXOutCallback callback) {
    cf::sdk::InterruptLockScope lock;
    AXOutCallback old = sFrameCallback;
    sFrameCallback = callback;
    return old;
}

void AXRegisterAuxACallback(AXAuxCallback callback, void* context) {
    cf::sdk::InterruptLockScope lock;
    sAuxCallback[0] = callback;
    sAuxContext[0] = context;
}

void AXRegisterAuxBCallback(AXAuxCallback callback, void* context) {
    cf::sdk::InterruptLockScope lock;
    sAuxCallback[1] = callback;
    sAuxContext[1] = context;
}

void AXRegisterAuxCCallback(AXAuxCallback callback, void* context) {
    cf::sdk::InterruptLockScope lock;
    sAuxCallback[2] = callback;
    sAuxContext[2] = context;
}

void AXGetAuxACallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[0];
    *context = sAuxContext[0];
}

void AXGetAuxBCallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[1];
    *context = sAuxContext[1];
}

void AXGetAuxCCallback(AXAuxCallback* callback, void** context) {
    *callback = sAuxCallback[2];
    *context = sAuxContext[2];
}

void AXSetAuxAReturnVolume(u16 volume) {
    sAuxReturnVolume[0] = volume;
}

void AXSetAuxBReturnVolume(u16 volume) {
    sAuxReturnVolume[1] = volume;
}

void AXSetAuxCReturnVolume(u16 volume) {
    sAuxReturnVolume[2] = volume;
}

u16 AXGetAuxAReturnVolume() {
    return sAuxReturnVolume[0];
}

u16 AXGetAuxBReturnVolume() {
    return sAuxReturnVolume[1];
}

u16 AXGetAuxCReturnVolume() {
    return sAuxReturnVolume[2];
}

void AXSetMasterVolume(u16 volume) {
    sMasterVolume = volume;
}

u16 AXGetMasterVolume(void) {
    return sMasterVolume;
}

void AXSetMode(u32 mode) {
    sMode = mode;
}

u32 AXGetMode() {
    return sMode;
}

// The one-pole low-pass coefficients for a cutoff frequency: y = (a * x + b * y[-1]) >> 15
void AXGetLpfCoefs(u16 freq, u16* a, u16* b) {
    f64 decay = std::exp(-2.0 * 3.14159265358979323846 * freq / AX_SAMPLE_RATE);
    u16 bc = static_cast<u16>(std::clamp(decay * 32768.0, 0.0, 32767.0));
    *b = bc;
    *a = static_cast<u16>(32767 - bc);
}

// ---------------------------------------------------------------------------------------------
// Voice parameter setters (the SDK's AXVPB.c): set the field and mark it for the next frame
// ---------------------------------------------------------------------------------------------
void AXSetVoiceSrcType(AXVPB* vpb, u32 type) {
    cf::sdk::InterruptLockScope lock;
    switch (type) {
    case AX_SRC_TYPE_NONE:
        vpb->pb.srcSelect = 2;
        break;
    case AX_SRC_TYPE_LINEAR:
        vpb->pb.srcSelect = 1;
        break;
    case AX_SRC_TYPE_4TAP_8K:
        vpb->pb.srcSelect = 0;
        vpb->pb.coefSelect = 0;
        break;
    case AX_SRC_TYPE_4TAP_12K:
        vpb->pb.srcSelect = 0;
        vpb->pb.coefSelect = 1;
        break;
    case AX_SRC_TYPE_4TAP_16K:
        vpb->pb.srcSelect = 0;
        vpb->pb.coefSelect = 2;
        break;
    }
    vpb->sync |= AX_PBSYNC_SELECT;
}

void AXSetVoiceState(AXVPB* vpb, u16 state) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.state = state;
    vpb->sync |= AX_PBSYNC_STATE;
}

void AXSetVoiceType(AXVPB* vpb, u16 type) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.type = type;
    vpb->sync |= AX_PBSYNC_TYPE;
}

void AXSetVoiceMix(AXVPB* vpb, AXPBMIX* mix) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.mix = *mix;
    vpb->sync |= AX_PBSYNC_MIX;
}

void AXSetVoiceVe(AXVPB* vpb, AXPBVE* ve) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.ve = *ve;
    vpb->sync |= AX_PBSYNC_VE;
}

void AXSetVoiceAddr(AXVPB* vpb, AXPBADDR* addr) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.addr = *addr;
    vpb->sync &= ~(AX_PBSYNC_LOOP_FLAG | AX_PBSYNC_LOOP_ADDR | AX_PBSYNC_END_ADDR | AX_PBSYNC_CURR_ADDR);
    vpb->sync |= AX_PBSYNC_ADDR;
}

void AXSetVoiceAdpcm(AXVPB* vpb, AXPBADPCM* adpcm) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.adpcm = *adpcm;
    vpb->sync |= AX_PBSYNC_ADPCM;
}

void AXSetVoiceSrc(AXVPB* vpb, AXPBSRC* src_) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.src = *src_;
    vpb->sync &= ~AX_PBSYNC_SRC_RATIO;
    vpb->sync |= AX_PBSYNC_SRC;
}

void AXSetVoiceSrcRatio(AXVPB* vpb, float ratio) {
    cf::sdk::InterruptLockScope lock;
    u32 r = static_cast<u32>(65536.0f * ratio);
    Split(r, &vpb->pb.src.ratioHi, &vpb->pb.src.ratioLo);
    if (!(vpb->sync & AX_PBSYNC_SRC)) {
        vpb->sync |= AX_PBSYNC_SRC_RATIO;
    }
}

void AXSetVoiceAdpcmLoop(AXVPB* vpb, AXPBADPCMLOOP* adpcmloop) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.adpcmLoop = *adpcmloop;
    vpb->sync |= AX_PBSYNC_ADPCM_LOOP;
}

void AXSetVoiceLpf(AXVPB* vpb, AXPBLPF* lpf) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.lpf = *lpf;
    vpb->sync |= AX_PBSYNC_LPF;
}

void AXSetVoiceLpfCoefs(AXVPB* vpb, u16 a0, u16 b0) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.lpf.a0 = a0;
    vpb->pb.lpf.b0 = b0;
    vpb->sync |= AX_PBSYNC_LPF_COEFS;
}

void AXSetVoiceRmtOn(AXVPB* vpb, u16 on) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.remote = on;
    vpb->sync |= AX_PBSYNC_REMOTE;
}

void AXSetVoiceRmtMix(AXVPB* vpb, AXPBRMTMIX* mix) {
    cf::sdk::InterruptLockScope lock;
    vpb->pb.rmtMix = *mix;
    vpb->sync |= AX_PBSYNC_RMTMIX;
}

void AXSetVoiceItdTarget(AXVPB* p, u16 lShift, u16 rShift) {
    cf::sdk::InterruptLockScope lock;
    p->pb.itd.targetShiftL = lShift;
    p->pb.itd.targetShiftR = rShift;
    p->sync |= AX_PBSYNC_ITD_SHIFT;
}

void AXSetVoiceLoop(AXVPB* p, u16 loop) {
    cf::sdk::InterruptLockScope lock;
    p->pb.addr.loopFlag = loop;
    if (!(p->sync & AX_PBSYNC_ADDR)) {
        p->sync |= AX_PBSYNC_LOOP_FLAG;
    }
}

void AXSetVoiceLoopAddr(AXVPB* p, u32 addr) {
    cf::sdk::InterruptLockScope lock;
    Split(addr, &p->pb.addr.loopAddressHi, &p->pb.addr.loopAddressLo);
    if (!(p->sync & AX_PBSYNC_ADDR)) {
        p->sync |= AX_PBSYNC_LOOP_ADDR;
    }
}

void AXSetVoiceEndAddr(AXVPB* p, u32 addr) {
    cf::sdk::InterruptLockScope lock;
    Split(addr, &p->pb.addr.endAddressHi, &p->pb.addr.endAddressLo);
    if (!(p->sync & AX_PBSYNC_ADDR)) {
        p->sync |= AX_PBSYNC_END_ADDR;
    }
}

void AXSetVoiceCurrentAddr(AXVPB* p, u32 addr) {
    cf::sdk::InterruptLockScope lock;
    Split(addr, &p->pb.addr.currentAddressHi, &p->pb.addr.currentAddressLo);
    if (!(p->sync & AX_PBSYNC_ADDR)) {
        p->sync |= AX_PBSYNC_CURR_ADDR;
    }
}

} // extern "C"
