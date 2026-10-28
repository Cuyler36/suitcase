#ifndef CF_REVOLUTION_WPAD_H
#define CF_REVOLUTION_WPAD_H
// PC: aurora's WPAD, plus the names of the decomp's include/lib/revolution/wpad.h that aurora does not
// define.
#include <revolution/types.h>
#include "../../../../extern/aurora/include/revolution/wpad.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WPAD_DEV_FUTURE 251
#define WPAD_DEV_NULL 254

#define WPAD_MOTOR_OFF WPAD_MOTOR_STOP
#define WPAD_MOTOR_ON WPAD_MOTOR_RUMBLE

#define WPAD_SPEAKER_OFF 0
#define WPAD_SPEAKER_ON 1
#define WPAD_SPEAKER_MUTE 2
#define WPAD_SPEAKER_UNMUTE 3
#define WPAD_SPEAKER_PLAY 4

#define WPAD_SENSOR_BAR_BOTTOM WPAD_SENSOR_BAR_POS_BOTTOM
#define WPAD_SENSOR_BAR_TOP WPAD_SENSOR_BAR_POS_TOP

// Not in aurora; stubbed in src/cf/sdk/stubs_wpad.cpp
s32 WPADReadFaceData(s32 chan, void* dst, u16 size, u16 src, WPADCallback cb);
s32 WPADWriteFaceData(s32 chan, void* dst, u16 size, u16 src, WPADCallback cb);

#ifdef __cplusplus
}
#endif

#endif
