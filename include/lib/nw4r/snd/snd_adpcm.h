#ifndef NW4R_SND_ADPCM_H
#define NW4R_SND_ADPCM_H
#include <nw4r/types_nw4r.h>

#include <revolution/ax.h>
#include <port/endian.h>

namespace nw4r {
namespace snd {
namespace detail {

struct AdpcmParam {
    u16 coef[16];   // at 0x0
    u16 gain;       // at 0x20
    u16 pred_scale; // at 0x22
    u16 yn1;        // at 0x24
    u16 yn2;        // at 0x26
}; // size 0x28

struct AdpcmLoopParam {
    u16 loop_pred_scale; // at 0x0
    u16 loop_yn1;        // at 0x2
    u16 loop_yn2;        // at 0x4
}; // size 0x6

struct AdpcmInfo {
    AdpcmParam param;         // at 0x0
    AdpcmLoopParam loopParam; // at 0x28
    u16 PADDING_0x2E;         // at 0x2E
}; // size 0x30

#if TARGET_PC
// AdpcmParam and AdpcmLoopParam as stored in RWAV/RSTM files (big-endian): the files' AdpcmParamSet
struct AdpcmParamSetData {
    BE(u16) coef[16];        // at 0x0
    BE(u16) gain;            // at 0x20
    BE(u16) pred_scale;      // at 0x22
    BE(u16) yn1;             // at 0x24
    BE(u16) yn2;             // at 0x26
    BE(u16) loop_pred_scale; // at 0x28
    BE(u16) loop_yn1;        // at 0x2A
    BE(u16) loop_yn2;        // at 0x2C

    void Read(AdpcmParam* pParam, AdpcmLoopParam* pLoopParam) const {
        for (int i = 0; i < 16; i++) {
            pParam->coef[i] = coef[i];
        }
        pParam->gain = gain;
        pParam->pred_scale = pred_scale;
        pParam->yn1 = yn1;
        pParam->yn2 = yn2;
        pLoopParam->loop_pred_scale = loop_pred_scale;
        pLoopParam->loop_yn1 = loop_yn1;
        pLoopParam->loop_yn2 = loop_yn2;
    }
}; // size 0x2E
#endif

s16 DecodeDspAdpcm(AXPBADPCM* pAdpcm, u8 bits);

} // namespace detail
} // namespace snd
} // namespace nw4r

#endif
