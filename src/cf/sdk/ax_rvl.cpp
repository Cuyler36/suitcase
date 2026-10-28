// Software AX: the Wii's audio DSP library, mixed on the CPU. aurora has no audio, so this implements
// the AX/AI calls the game makes (nw4r snd and SoundManager):
//
// - Voices: AXAcquireVoice/AXFreeVoice hand out AX_VOICE_MAX parameter blocks with the SDK's priority
//   stealing (the stolen voice's callback runs first). Game code writes vpb->pb and sets vpb->sync,
//   as with the SDK; each frame the synced fields are copied to the voice's DSP copy, the DSP copy is
//   rendered, and the state and current address (what the game polls) are copied back.
// - A frame is AX_SAMPLES_PER_FRAME (96) samples at 32 kHz, 3 ms. Per voice: DSP-ADPCM / PCM16
//   (big-endian) / PCM8 decoding with loops, sample rate conversion, the low-pass and biquad
//   volume envelope, filters, and the mix into the main and aux A/B/C buses (L, R, S). The aux
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
// (Wii Remote speaker) mix, ITD, surround/DPL2 output (S is dropped in stereo).
#include <dolphin/ai.h>
#include <revolution/ax.h>
#include <revolution/os.h>

#include <borealis/log_c.h>
#include <cf/audio.h>

#include "interrupt_lock.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

constexpr int FRAME_SAMPLES = AX_SAMPLES_PER_FRAME;
// srcSelect as the DSP reads it (nw4r's AxVoiceParamBlock::SetVoiceSrcType): 0 polyphase, 1 linear,
// 2 none. (The SDK's AX_SRC_TYPE_* enum is the API's numbering, not the parameter block's.)
constexpr u16 AX_SRC_TYPE_NONE_PB = 2;
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
// The host's output volume (cf/audio.h), as AX volumes are: 0x8000 is 1
std::atomic<s32> sHostVolume{AX_MAX_VOLUME};
u32 sMode = AX_OUTPUT_STEREO;

// Bus buffers: main L/R/S, then L/R/S of aux A, B and C
s32 sMain[AX_STEREO_MAX][FRAME_SAMPLES];
s32 sAux[AUX_BUS_NUM][AX_STEREO_MAX][FRAME_SAMPLES];
// SDK AXAux.c: DSP write, DSP read, CPU process start at slots 0, 1, 2.
s32 sAuxRing[AUX_BUS_NUM][3][AX_STEREO_MAX][FRAME_SAMPLES];
int sAuxFrame = 0;
s32 sDepop[12] = {};

u32 Join(u16 hi, u16 lo) {
    return (static_cast<u32>(hi) << 16) | lo;
}

void Split(u32 value, u16* hi, u16* lo) {
    *hi = static_cast<u16>(value >> 16);
    *lo = static_cast<u16>(value & 0xFFFF);
}

s16 Clamp16(s64 value) {
    return static_cast<s16>(std::clamp<s64>(value, -32768, 32767));
}

s32 Clamp32(s64 value) {
    return static_cast<s32>(std::clamp<s64>(value, -2147483647LL - 1, 2147483647LL));
}

// Capture the last contribution once, before resetting/reusing a DSP voice.
void DepopVoice(AXPB& pb) {
    const s16 values[12] = {pb.dpop.aL, pb.dpop.aR, pb.dpop.aAuxAL, pb.dpop.aAuxAR,
                           pb.dpop.aAuxBL, pb.dpop.aAuxBR, pb.dpop.aAuxCL, pb.dpop.aAuxCR,
                           pb.dpop.aS, pb.dpop.aAuxAS, pb.dpop.aAuxBS, pb.dpop.aAuxCS};
    for (int c = 0; c < 12; c++) {
        sDepop[c] += values[c];
    }
    std::memset(&pb.dpop, 0, sizeof(pb.dpop));
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
    if (sync & AX_PBSYNC_VE_DELTA) {
        vpb.pb.ve.currentVolume = dsp.ve.currentVolume;
        dsp.ve.currentDelta = pb.ve.currentDelta;
    } else if (sync & AX_PBSYNC_VE) {
        dsp.ve = pb.ve;
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
            sample = p != nullptr ? static_cast<s16>(static_cast<s8>(p[0]) * 256) : 0;
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
        s64 value = static_cast<s64>(nibble) * scale * 2048 + 1024 +
                    static_cast<s64>(coef1) * yn1 + static_cast<s64>(coef2) * yn2;
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

// DSP polyphase SRC: three tables of 128 phases, four signed Q15 taps per phase.
// An optional 4096-byte big-endian DSP coefficient ROM supplies exact hardware taps.
// The built-in fallback is a windowed-sinc approximation, not a console ROM dump.
constexpr int SRC_PHASES = 128;
constexpr int SRC_TABLES = 3;
constexpr f64 kPi = 3.14159265358979323846;

struct SrcTables {
    s16 c[SRC_TABLES][SRC_PHASES][4];

    static f64 BesselI0(f64 x) {
        f64 sum = 1.0, term = 1.0;
        for (int n = 1; n < 32; n++) {
            term *= x * x / (4.0 * n * n);
            sum += term;
        }
        return sum;
    }

    SrcTables() {
        // Hamming / Kaiser / Hamming prototype windows, with global gain normalization.
        // See docs/audio.md for the reference and the limits of this fallback.
        const f64 bandwidths[SRC_TABLES] = {0.5, 0.75, 1.0};
        for (int t = 0; t < SRC_TABLES; t++) {
            f64 prototype[512];
            for (int n = 0; n < 512; n++) {
                const f64 position = 2.0 * n / 511.0 - 1.0;
                const f64 x = 2.0 * position * bandwidths[t];
                const f64 sinc = x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const f64 window = t == 1
                    ? BesselI0(2.25 * kPi * std::sqrt(std::max(0.0, 1.0 - position * position))) /
                          BesselI0(2.25 * kPi)
                    : 0.54 - 0.46 * std::cos(2.0 * kPi * n / 511.0);
                prototype[n] = sinc * window;
            }
            f64 gain = 0.0;
            for (int phase = 0; phase < SRC_PHASES; phase++) {
                f64 sum = 0.0;
                for (int tap = 0; tap < 4; tap++) {
                    sum += prototype[tap * SRC_PHASES + 127 - phase];
                }
                gain = std::max(gain, sum);
            }
            for (int phase = 0; phase < SRC_PHASES; phase++) {
                for (int tap = 0; tap < 4; tap++) {
                    c[t][phase][tap] = static_cast<s16>(std::nearbyint(
                        prototype[tap * SRC_PHASES + 127 - phase] * 32767.0 / gain));
                }
            }
        }
    }

    bool Load(const char* path) {
        FILE* file = std::fopen(path, "rb");
        if (file == nullptr) {
            return false;
        }
        u8 bytes[4096];
        const size_t count = std::fread(bytes, 1, sizeof(bytes), file);
        const bool valid = count == sizeof(bytes) && std::fgetc(file) == EOF && !std::ferror(file);
        std::fclose(file);
        if (!valid) {
            return false;
        }
        for (int t = 0; t < SRC_TABLES; t++) {
            for (int phase = 0; phase < SRC_PHASES; phase++) {
                for (int tap = 0; tap < 4; tap++) {
                    const int offset = 2 * ((t * SRC_PHASES + phase) * 4 + tap);
                    c[t][phase][tap] = static_cast<s16>((bytes[offset] << 8) | bytes[offset + 1]);
                }
            }
        }
        return true;
    }
};

SrcTables& GetSrcTables() {
    static SrcTables tables;
    return tables;
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
    const s16(*coefs)[4] = GetSrcTables().c[pb.coefSelect < SRC_TABLES ? pb.coefSelect : SRC_TABLES - 1];

    for (int i = 0; i < FRAME_SAMPLES; i++) {
        if (pb.srcSelect == AX_SRC_TYPE_NONE_PB) {
            // Bypass reads directly, without the resampler's history delay.
            out[i] = reader.Next();
            h[0] = h[1];
            h[1] = h[2];
            h[2] = h[3];
            h[3] = out[i];
            continue;
        }
        // The DSP advances BEFORE evaluating the phase and taps.
        const u64 position = static_cast<u64>(frac) + ratio;
        const u32 advance = static_cast<u32>(position >> 16);
        frac = static_cast<u32>(position & 0xFFFF);
        for (u32 n = 0; n < advance; n++) {
            h[0] = h[1];
            h[1] = h[2];
            h[2] = h[3];
            h[3] = reader.Next();
        }
        if (pb.srcSelect == 0) {
            const s16* c = coefs[frac >> 9];
            s64 sum = 0;
            for (int tap = 0; tap < 4; tap++) {
                sum += static_cast<s64>(c[tap]) * h[tap];
            }
            out[i] = Clamp16(sum >> 15);
        } else {
            out[i] = Clamp16((static_cast<s64>(h[0]) * (65536 - frac) +
                              static_cast<s64>(h[1]) * frac) >> 16);
        }
    }

    reader.Store();
    pb.src.currentAddressFrac = static_cast<u16>(frac);
    for (int i = 0; i < 4; i++) {
        pb.src.last_samples[i] = static_cast<u16>(h[i]);
    }

    u16 volume = pb.ve.currentVolume;
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        out[i] = Clamp16((static_cast<s64>(out[i]) * volume) >> 15);
        volume = static_cast<u16>(volume + pb.ve.currentDelta);
    }
    pb.ve.currentVolume = volume;

    if (pb.lpf.on == AX_PB_LPF_ON) {
        s32 a0 = pb.lpf.a0;
        s32 b0 = pb.lpf.b0;
        s32 yn1 = static_cast<s16>(pb.lpf.yn1);
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            yn1 = Clamp16((static_cast<s64>(a0) * out[i] + static_cast<s64>(b0) * yn1) >> 15);
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
            const s64 sum = static_cast<s64>(b0) * x + static_cast<s64>(b1) * xn1 +
                            static_cast<s64>(b2) * xn2 + static_cast<s64>(a1) * yn1 +
                            static_cast<s64>(a2) * yn2;
            // DSP CLRL rounding: nearest, with half-way cases rounded to even.
            const s64 rounded = (sum + 8191 + ((sum >> 14) & 1)) >> 14;
            s32 y = Clamp16(rounded);
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
}

// AXPBMIX holds (volume, delta) pairs in this order; each goes to a bus channel
struct MixTarget {
    s32* bus;
    u32 enableFlag;
    u32 deltaFlag;
};

const MixTarget sMixTargets[12] = {
    {sMain[AX_STEREO_L], AX_MIXER_CTRL_L, AX_MIXER_CTRL_DELTA},
    {sMain[AX_STEREO_R], AX_MIXER_CTRL_R, AX_MIXER_CTRL_DELTA},
    {sAux[0][AX_STEREO_L], AX_MIXER_CTRL_AL, AX_MIXER_CTRL_DELTA_A},
    {sAux[0][AX_STEREO_R], AX_MIXER_CTRL_AR, AX_MIXER_CTRL_DELTA_A},
    {sAux[1][AX_STEREO_L], AX_MIXER_CTRL_BL, AX_MIXER_CTRL_DELTA_B},
    {sAux[1][AX_STEREO_R], AX_MIXER_CTRL_BR, AX_MIXER_CTRL_DELTA_B},
    {sAux[2][AX_STEREO_L], AX_MIXER_CTRL_CL, AX_MIXER_CTRL_DELTA_C},
    {sAux[2][AX_STEREO_R], AX_MIXER_CTRL_CR, AX_MIXER_CTRL_DELTA_C},
    {sMain[AX_STEREO_S], AX_MIXER_CTRL_S, AX_MIXER_CTRL_DELTA_S},
    {sAux[0][AX_STEREO_S], AX_MIXER_CTRL_AS, AX_MIXER_CTRL_DELTA_AS},
    {sAux[1][AX_STEREO_S], AX_MIXER_CTRL_BS, AX_MIXER_CTRL_DELTA_BS},
    {sAux[2][AX_STEREO_S], AX_MIXER_CTRL_CS, AX_MIXER_CTRL_DELTA_CS},
};

void MixVoice(AXPB& pb, const s32 in[FRAME_SAMPLES]) {
    s16* depop[12] = {&pb.dpop.aL, &pb.dpop.aR, &pb.dpop.aAuxAL, &pb.dpop.aAuxAR,
                      &pb.dpop.aAuxBL, &pb.dpop.aAuxBR, &pb.dpop.aAuxCL, &pb.dpop.aAuxCR,
                      &pb.dpop.aS, &pb.dpop.aAuxAS, &pb.dpop.aAuxBS, &pb.dpop.aAuxCS};
    u16* mix = reinterpret_cast<u16*>(&pb.mix);
    for (int c = 0; c < 12; c++) {
        const MixTarget& target = sMixTargets[c];
        if (!(pb.mixerCtrl & target.enableFlag)) {
            continue;
        }
        u16 volume = mix[c * 2];
        const s16 delta = (pb.mixerCtrl & target.deltaFlag) ? static_cast<s16>(mix[c * 2 + 1]) : 0;
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            const s16 sample = Clamp16((static_cast<s64>(in[i]) * volume) >> 15);
            target.bus[i] += sample;
            volume = static_cast<u16>(volume + delta);
            *depop[c] = sample;
        }
        mix[c * 2] = volume;
    }
}

void RenderDepop() {
    // AXSPB.c: ramp each accumulated bus contribution by at most 20/sample.
    for (int c = 0; c < 12; c++) {
        const s32 step = std::clamp(sDepop[c] / FRAME_SAMPLES, -20, 20);
        if (step == 0) {
            sDepop[c] = 0;
            continue;
        }
        s32 value = sDepop[c];
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            sMixTargets[c].bus[i] += value;
            value -= step;
        }
        sDepop[c] = value;
    }
}

void ProcessAux() {
    const int write = sAuxFrame;
    const int read = (sAuxFrame + 1) % 3;
    const int cpu = (sAuxFrame + 2) % 3;
    for (int b = 0; b < AUX_BUS_NUM; b++) {
        if (sAuxCallback[b] == nullptr) {
            std::memset(sAuxRing[b], 0, sizeof(sAuxRing[b]));
            continue;
        }
        std::memcpy(sAuxRing[b][write], sAux[b], sizeof(sAux[b]));
        const s32 volume = sAuxReturnVolume[b];
        for (int ch = 0; ch < AX_STEREO_MAX; ch++) {
            for (int i = 0; i < FRAME_SAMPLES; i++) {
                const s64 wet = (static_cast<s64>(sAuxRing[b][read][ch][i]) * volume) >> 15;
                sMain[ch][i] = Clamp32(static_cast<s64>(sMain[ch][i]) + wet);
            }
        }
        void* chans[AX_STEREO_MAX] = {sAuxRing[b][cpu][AX_STEREO_L], sAuxRing[b][cpu][AX_STEREO_R],
                                     sAuxRing[b][cpu][AX_STEREO_S]};
        sAuxCallback[b](chans, sAuxContext[b]);
    }
    sAuxFrame = (sAuxFrame + 1) % 3;
}

// CF_AX_LOG=1: logs each voice as it starts (how it is resampled, filtered and mixed), to compare
// sounds with the Wii's
bool LogVoices() {
    static const bool log = std::getenv("CF_AX_LOG") != nullptr;
    return log;
}

void LogVoiceStart(int v, const AXVPB& vpb, const AXPB& pb) {
    const AXPBMIX& m = pb.mix;
    const AXPBRMTMIX& r = pb.rmtMix;
    borealis_log_printf(BOREALIS_LOG_INFO, "cf::ax",
                        "voice %2d start: prio %2u fmt %u src %u coef %u ratio %.4f lpf %u biquad %u "
                        "vol %04X main %04X/%04X auxA %04X/%04X auxB %04X/%04X auxC %04X/%04X "
                        "remote %u rmt %04X/%04X/%04X/%04X aux %04X/%04X/%04X/%04X",
                        v, vpb.priority, pb.addr.format, pb.srcSelect, pb.coefSelect,
                        Join(pb.src.ratioHi, pb.src.ratioLo) / 65536.0, pb.lpf.on, pb.biquad.on, pb.ve.currentVolume,
                        m.vL, m.vR, m.vAuxAL, m.vAuxAR, m.vAuxBL, m.vAuxBR, m.vAuxCL, m.vAuxCR, pb.remote,
                        r.vMain0, r.vMain1, r.vMain2, r.vMain3, r.vAux0, r.vAux1, r.vAux2, r.vAux3);
}

// One AX frame; out (FRAME_SAMPLES interleaved stereo samples) may be null
void RunFrame(s16* out) {
    cf::sdk::InterruptLockScope lock;

    std::memset(sMain, 0, sizeof(sMain));
    std::memset(sAux, 0, sizeof(sAux));

    s32 voiceOut[FRAME_SAMPLES];
    for (int v = 0; v < AX_VOICE_MAX; v++) {
        AXVPB& vpb = sVoices[v];
        if (vpb.depop) {
            DepopVoice(sDsp[v]);
            vpb.depop = false;
        }
        if (vpb.priority == AX_PRIORITY_FREE) {
            continue;
        }
        AXPB& dsp = sDsp[v];
        const u16 prevState = dsp.state;
        ApplySync(vpb, dsp);
        if (prevState != AX_VOICE_RUN && dsp.state == AX_VOICE_RUN && LogVoices()) {
            LogVoiceStart(v, vpb, dsp);
        }
        if (dsp.state == AX_VOICE_RUN) {
            RenderVoice(dsp, voiceOut);
            MixVoice(dsp, voiceOut);
        }
        // What the game polls: whether the voice still runs, and where it is
        vpb.pb.state = dsp.state;
        vpb.pb.ve.currentVolume = dsp.ve.currentVolume;
        vpb.pb.addr.currentAddressHi = dsp.addr.currentAddressHi;
        vpb.pb.addr.currentAddressLo = dsp.addr.currentAddressLo;
    }

    RenderDepop();
    ProcessAux();

    if (out != nullptr) {
        s32 master = (static_cast<s32>(sMasterVolume) * sHostVolume.load(std::memory_order_relaxed)) >> 15;
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            out[i * 2 + 0] = Clamp16((static_cast<s64>(sMain[AX_STEREO_L][i]) * master) >> 15);
            out[i * 2 + 1] = Clamp16((static_cast<s64>(sMain[AX_STEREO_R][i]) * master) >> 15);
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

namespace cf {
namespace audio {

void SetVolume(float volume) {
    sHostVolume.store(static_cast<s32>(std::clamp(volume, 0.0f, 1.0f) * AX_MAX_VOLUME + 0.5f),
                      std::memory_order_relaxed);
}

} // namespace audio
} // namespace cf

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
        std::memset(sMain, 0, sizeof(sMain));
        std::memset(sAux, 0, sizeof(sAux));
        std::memset(sAuxRing, 0, sizeof(sAuxRing));
        std::memset(sDepop, 0, sizeof(sDepop));
        sAuxFrame = 0;
        sMasterVolume = AX_MAX_VOLUME;
        for (u16& volume : sAuxReturnVolume) {
            volume = AX_MAX_VOLUME;
        }
        // Prepare coefficients before starting the audio callback; no file I/O on that thread.
        GetSrcTables() = SrcTables();
        const char* coefPath = std::getenv("CF_AX_COEFFICIENTS");
        if (coefPath == nullptr) {
            coefPath = "dsp_coef.bin";
        }
        const bool loaded = GetSrcTables().Load(coefPath);
        borealis_log_printf(BOREALIS_LOG_INFO, "cf::ax", loaded
            ? "loaded DSP resampling coefficients from %s"
            : "DSP coefficients unavailable/invalid at %s; using approximate resampling tables", coefPath);
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
        if (vpb->pb.state == AX_VOICE_RUN || vpb->depop) {
            DepopVoice(sDsp[vpb->index]);
            vpb->depop = false;
        }
        if (vpb->callback != nullptr) {
            vpb->callback(vpb);
        }
    }
    // A freed slot may be reacquired before the next frame services its depop flag.
    if (vpb->depop) {
        DepopVoice(sDsp[vpb->index]);
        vpb->depop = false;
    }
    vpb->priority = priority;
    vpb->callback = callback;
    vpb->userContext = userContext;
    ResetVoice(vpb);
    return vpb;
}

void AXFreeVoice(AXVPB* p) {
    cf::sdk::InterruptLockScope lock;
    if (p->pb.state == AX_VOICE_RUN) {
        p->depop = true;
    }
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
    if (sAuxCallback[0] != callback || sAuxContext[0] != context) {
        std::memset(sAuxRing[0], 0, sizeof(sAuxRing[0]));
    }
    sAuxCallback[0] = callback;
    sAuxContext[0] = context;
}

void AXRegisterAuxBCallback(AXAuxCallback callback, void* context) {
    cf::sdk::InterruptLockScope lock;
    if (sAuxCallback[1] != callback || sAuxContext[1] != context) {
        std::memset(sAuxRing[1], 0, sizeof(sAuxRing[1]));
    }
    sAuxCallback[1] = callback;
    sAuxContext[1] = context;
}

void AXRegisterAuxCCallback(AXAuxCallback callback, void* context) {
    cf::sdk::InterruptLockScope lock;
    if (sAuxCallback[2] != callback || sAuxContext[2] != context) {
        std::memset(sAuxRing[2], 0, sizeof(sAuxRing[2]));
    }
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
    cf::sdk::InterruptLockScope lock;
    sAuxReturnVolume[0] = volume;
}

void AXSetAuxBReturnVolume(u16 volume) {
    cf::sdk::InterruptLockScope lock;
    sAuxReturnVolume[1] = volume;
}

void AXSetAuxCReturnVolume(u16 volume) {
    cf::sdk::InterruptLockScope lock;
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
    cf::sdk::InterruptLockScope lock;
    sMasterVolume = std::min<u16>(volume, AX_MAX_VOLUME);
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
    // Preserve the SDK's single-precision intermediates (AXVPB.c).
    const f32 angle = 2.0f * 3.14159265358979323846f * freq / AX_SAMPLE_RATE;
    const f32 term = 2.0f - static_cast<f32>(std::cos(static_cast<f64>(angle)));
    const f32 pole = static_cast<f32>(std::sqrt(static_cast<f64>(term * term - 1.0f))) - term;
    *b = static_cast<u16>(32768.0f * -pole);
    *a = static_cast<u16>(32767 - *b);
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
    if (state == AX_VOICE_STOP && vpb->pb.state == AX_VOICE_RUN) {
        vpb->depop = true;
    }
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
    const u16* values = reinterpret_cast<const u16*>(mix);
    u32 ctrl = 0;
    for (int c = 0; c < 12; c++) {
        if (values[c * 2] != 0 || values[c * 2 + 1] != 0) {
            ctrl |= sMixTargets[c].enableFlag;
        }
        if (values[c * 2 + 1] != 0) {
            ctrl |= sMixTargets[c].deltaFlag;
        }
    }
    vpb->pb.mixerCtrl = ctrl;
    vpb->sync |= AX_PBSYNC_MIX | AX_PBSYNC_MIXER_CTRL;
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
