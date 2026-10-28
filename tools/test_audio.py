"""Compile the production software AX with fake host I/O and exercise it without assets.

Uses the real AX parameter headers and the entire ax_rvl.cpp translation unit;
only SDL, logging, address translation, and the interrupt lock are replaced.
Run: python tools/test_audio.py (requires a C++17 compiler environment).
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TYPES = r'''
#pragma once
#include <cstdint>
using u8=uint8_t; using s8=int8_t; using u16=uint16_t; using s16=int16_t;
using u32=uint32_t; using s32=int32_t; using u64=uint64_t; using s64=int64_t;
using f32=float; using f64=double;
'''
STUBS = r'''
#include <revolution/ax.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <array>
#include <random>
using AIDCallback = void (*)();
namespace cf::sdk { struct InterruptLockScope {}; }
constexpr int BOREALIS_LOG_INFO=0, BOREALIS_LOG_ERROR=1;
void borealis_log_printf(int, const char*, const char*, ...) {}
static u8 sampleMemory[65536];
void* CFPhysicalToCached(u32 addr) { assert(addr < sizeof(sampleMemory)); return sampleMemory + addr; }
#define SDLCALL
struct SDL_AudioStream {};
struct SDL_AudioSpec { int format, channels, freq; };
constexpr int SDL_INIT_AUDIO=1, SDL_AUDIO_S16=1, SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK=1;
bool SDL_InitSubSystem(int) { return true; }
const char* SDL_GetError() { return "test"; }
SDL_AudioStream* SDL_OpenAudioDeviceStream(int,const SDL_AudioSpec*,
    void (*)(void*,SDL_AudioStream*,int,int),void*) { static SDL_AudioStream stream; return &stream; }
void SDL_ResumeAudioStreamDevice(SDL_AudioStream*) {}
void SDL_DestroyAudioStream(SDL_AudioStream*) {}
void SDL_QuitSubSystem(int) {}
void SDL_PutAudioStreamData(SDL_AudioStream*,const void*,size_t) {}
'''
TESTS = r'''
static void Reset() {
    AXQuit();
    AXInit();
    cf::audio::SetVolume(1.0f);
    std::memset(sampleMemory, 0, sizeof(sampleMemory));
}
static void PutSample(int i, s16 value) {
    sampleMemory[2*i] = static_cast<u16>(value) >> 8;
    sampleMemory[2*i+1] = value & 255;
}
static AXPB Voice(int length=4096) {
    AXPB pb{};
    pb.state = AX_VOICE_RUN;
    pb.srcSelect = 2;
    pb.src.ratioHi = 1;
    pb.ve.currentVolume = 0x8000;
    pb.addr.format = AX_SAMPLE_FORMAT_PCM_S16;
    pb.addr.endAddressLo = length - 1;
    pb.addr.loopFlag = 1;
    pb.mix.vL = pb.mix.vR = 0x8000;
    pb.mixerCtrl = AX_MIXER_CTRL_L | AX_MIXER_CTRL_R;
    return pb;
}
static void TestLpf() {
    // Execute the SDK equation with its float intermediates at every usable cutoff.
    for (int freq=1; freq<=16000; freq++) {
        f32 t = 2.0f - (f32)std::cos(2 * 3.14159265358979323846f * freq / AX_SAMPLE_RATE);
        f32 p = (f32)std::sqrt(t*t - 1.0f) - t;
        u16 b = static_cast<u16>(32768 * -p), a = static_cast<u16>(32767-b);
        u16 actualA, actualB;
        AXGetLpfCoefs(freq,&actualA,&actualB);
        assert(actualA==a && actualB==b);
    }
}
static void TestOverflowAndOrder() {
    Reset();
    for (int i=0; i<4096; i++) PutSample(i,30000);
    for (int i=0; i<3; i++) AXAcquireVoice(10,nullptr,0)->pb=Voice();
    s16 out[FRAME_SAMPLES*2];
    RunFrame(out);
    for (s16 sample:out) assert(sample==32767);
    cf::audio::SetVolume(0.25f);
    RunFrame(out);
    for (s16 sample:out) assert(sample==22500); // Preserve headroom until output gain.

    Reset();
    // Filter history must be accumulated AFTER the envelope, not at full input gain.
    PutSample(0,16000);
    AXPB pb=Voice(); pb.ve.currentVolume=0x4000;
    pb.lpf.on=1; pb.lpf.a0=pb.lpf.b0=0x4000;
    s32 samples[FRAME_SAMPLES]; RenderVoice(pb,samples);
    assert(samples[0]==4000 && samples[1]==2000);

    // Existing LPF biquad preset; adversarial but valid full-scale PCM input.
    std::mt19937 rng(42);
    std::vector<s16> input(4096);
    for (int i=0; i<4096; i++) { input[i]=(rng()&1)?32767:-32768; PutSample(i,input[i]); }
    pb=Voice(); pb.biquad.on=2;
    pb.biquad.b0=0x3ab3; pb.biquad.b1=0x7566; pb.biquad.b2=0x3ab3;
    pb.biquad.a1=0x83b8; pb.biquad.a2=0xc391;
    s64 x1=0,x2=0,y1=0,y2=0; bool exceeded=false;
    for (int frame=0;frame<40;frame++) {
        RenderVoice(pb,samples);
        for(int i=0;i<FRAME_SAMPLES;i++) {
            s64 x=input[frame*FRAME_SAMPLES+i];
            s64 sum=15027*x+30054*x1+15027*x2-31816*y1-15471*y2;
            exceeded |= sum>2147483647LL || sum<(-2147483647LL-1);
            // Independent round-to-nearest-even using double (all integers fit exactly).
            s64 y=static_cast<s64>(std::nearbyint(sum/16384.0));
            y=std::clamp<s64>(y,-32768,32767);
            assert(samples[i]==y);
            x2=x1; x1=x; y2=y1; y1=y;
        }
    }
    assert(exceeded);
}
static void TestDecodingAndSrc() {
    Reset(); s32 out[FRAME_SAMPLES];
    for(int i=0;i<4096;i++) PutSample(i,1000+i);
    AXPB pb=Voice(); pb.src.ratioHi=5;
    RenderVoice(pb,out);
    assert(out[0]==1000 && out[95]==1095 && pb.addr.currentAddressLo==96);
    assert(pb.src.last_samples[0]==1092 && pb.src.last_samples[3]==1095);
    pb=Voice(); pb.srcSelect=1; RenderVoice(pb,out);
    assert(out[0]==0 && out[1]==0 && out[2]==0 && out[3]==1000);
    pb=Voice(); pb.srcSelect=1; pb.src.ratioHi=0; pb.src.ratioLo=0x8000;
    pb.src.last_samples[0]=static_cast<u16>(-3); pb.src.last_samples[1]=0;
    RenderVoice(pb,out); assert(out[0]==-2); // Floor negative half samples, not truncate.

    // Deterministic synthetic coefficient ROM checks file endian, phase/table/tap selection.
    std::array<u8,4096> rom{};
    for(int t=0;t<3;t++) for(int phase=0;phase<128;phase++) {
        int offset=2*((t*128+phase)*4+t);
        rom[offset]=0x40; // 0.5 at a different tap in each table.
    }
    FILE* f=std::fopen("test-coefs.bin","wb"); assert(f);
    assert(std::fwrite(rom.data(),1,rom.size(),f)==rom.size()); std::fclose(f);
    assert(GetSrcTables().Load("test-coefs.bin"));
    for(int t=0;t<3;t++) {
        pb=Voice(); pb.srcSelect=0; pb.coefSelect=t;
        pb.src.last_samples[0]=2000; pb.src.last_samples[1]=4000;
        pb.src.last_samples[2]=6000; pb.src.last_samples[3]=8000;
        RenderVoice(pb,out); assert(out[0]==2000+t*1000);
    }
    f=std::fopen("test-coefs.bin","wb"); assert(f); std::fputc(0,f); std::fclose(f);
    assert(!GetSrcTables().Load("test-coefs.bin"));
    assert(GetSrcTables().c[0][0][0]==0x4000); // Invalid load is atomic.
    std::remove("test-coefs.bin");

    Reset(); sampleMemory[0]=0x80; sampleMemory[1]=0x7f;
    pb=Voice(2); pb.addr.format=AX_SAMPLE_FORMAT_PCM_S8;
    RenderVoice(pb,out); assert(out[0]==-32768 && out[1]==32512 && out[2]==-32768);
    Reset(); sampleMemory[0]=0; sampleMemory[1]=0x78;
    pb=Voice(); pb.addr.format=AX_SAMPLE_FORMAT_DSP_ADPCM;
    pb.addr.currentAddressLo=2; pb.addr.endAddressLo=3; pb.addr.loopAddressLo=2;
    pb.addr.loopFlag=0;
    RenderVoice(pb,out); assert(out[0]==7 && out[1]==-8 && out[2]==0);
    assert(pb.state==AX_VOICE_STOP && pb.addr.currentAddressLo==2);
    // Large ADPCM predictor sum must saturate without a 32-bit intermediate overflow.
    pb=Voice(); pb.addr.format=AX_SAMPLE_FORMAT_DSP_ADPCM;
    pb.addr.currentAddressLo=2; pb.adpcm.pred_scale=15;
    pb.adpcm.a[0][0]=pb.adpcm.a[0][1]=32767;
    pb.adpcm.yn1=pb.adpcm.yn2=32767;
    SampleReader reader(pb); assert(reader.Next()==32767);
}
static void Passthrough(void*,void*) {}
static void TestAux() {
    Reset(); AXRegisterAuxACallback(Passthrough,nullptr);
    sAux[0][0][0]=90000;
    ProcessAux(); assert(sMain[0][0]==0);
    std::memset(sAux,0,sizeof(sAux));
    ProcessAux(); assert(sMain[0][0]==0);
    ProcessAux(); assert(sMain[0][0]==90000); // Two AX frames = 6 ms.
    AXRegisterAuxACallback(nullptr,nullptr);
    for(auto& slot:sAuxRing[0]) for(auto& channel:slot) for(s32 x:channel) assert(x==0);
}
static void TestDepopAndMix() {
    Reset(); for(int i=0;i<4096;i++) PutSample(i,10000);
    AXVPB* v=AXAcquireVoice(10,nullptr,0); v->pb=Voice();
    s16 out[FRAME_SAMPLES*2]; RunFrame(out); assert(out[191]==10000);
    AXSetVoiceState(v,AX_VOICE_STOP); RunFrame(out);
    assert(out[0]==10000 && out[190]==8100);
    RunFrame(out); assert(out[0]==8080);
    for(int i=0;i<8;i++) RunFrame(out);
    for(s16 x:out) assert(x==0);

    Reset(); for(int i=0;i<4096;i++) PutSample(i,10000);
    v=AXAcquireVoice(10,nullptr,0); v->pb=Voice(); RunFrame(out);
    AXFreeVoice(v);
    AXVPB* reused=AXAcquireVoice(10,nullptr,0); assert(reused==v);
    RunFrame(out); assert(out[0]==10000); // Reuse cannot discard the pending fade.

    Reset(); for(int i=0;i<4096;i++) PutSample(i,10000);
    for(int i=0;i<AX_VOICE_MAX;i++) AXAcquireVoice(1,nullptr,0)->pb=Voice();
    RunFrame(out); AXVPB* stolen=AXAcquireVoice(2,nullptr,0); assert(stolen);
    assert(sDepop[0]==10000 && sDepop[1]==10000);

    Reset(); AXPB pb=Voice(); s32 in[FRAME_SAMPLES]; std::fill_n(in,FRAME_SAMPLES,20000);
    pb.mixerCtrl=0; MixVoice(pb,in); assert(sMain[0][0]==0);
    pb.mixerCtrl=AX_MIXER_CTRL_L; pb.mix.vL=0xffff; MixVoice(pb,in);
    assert(sMain[0][0]==32767 && pb.dpop.aL==32767);
    v=AXAcquireVoice(10,nullptr,0); AXPBMIX mix{}; mix.vDeltaL=1;
    AXSetVoiceMix(v,&mix);
    assert(v->pb.mixerCtrl==(AX_MIXER_CTRL_L|AX_MIXER_CTRL_DELTA));
    AXSetMasterVolume(0xffff); assert(AXGetMasterVolume()==AX_MAX_VOLUME);
}
int main() {
    TestLpf(); TestOverflowAndOrder(); TestDecodingAndSrc(); TestAux(); TestDepopAndMix();
    AXQuit(); std::cout << "Audio regression checks passed\n";
}
'''


def main():
    source = (ROOT / "src/cf/sdk/ax_rvl.cpp").read_text(encoding="utf-8")
    # Retain all standard-library includes; replace only host-facing includes.
    source = re.sub(r'^#include [<"](?:dolphin/|revolution/|borealis/|cf/|SDL3/|interrupt_lock)[^\n]+\n', '', source, flags=re.M)
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cf-audio-", dir=build) as directory:
        directory = Path(directory)
        (directory / "revolution").mkdir()
        (directory / "revolution/types.h").write_text(TYPES)
        cpp = directory / "audio_test.cpp"
        exe = directory / ("audio_test.exe" if os.name == "nt" else "audio_test")
        cpp.write_text(STUBS + source + TESTS, encoding="utf-8")
        if os.name == "nt":
            cmd = ["cl", "/nologo", "/EHsc", "/std:c++17", "/DTARGET_PC=1", "/D_CRT_SECURE_NO_WARNINGS",
                   "/I"+str(directory), "/I"+str(ROOT/"include/lib"), str(cpp), "/Fe:"+str(exe)]
        else:
            cmd = ["c++", "-std=c++17", "-DTARGET_PC=1", "-I"+str(directory), "-I"+str(ROOT/"include/lib"),
                   "-fsanitize=undefined", "-fno-sanitize-recover=all", str(cpp), "-o", str(exe)]
        subprocess.run(cmd, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
