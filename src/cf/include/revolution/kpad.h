#ifndef CF_REVOLUTION_KPAD_H
#define CF_REVOLUTION_KPAD_H
// PC: aurora's KPAD, plus what the decomp's include/lib/revolution/kpad.h has that aurora does not.
#include <revolution/mtx.h>
#include <revolution/wpad.h>
#include "../../../../extern/aurora/include/revolution/kpad.h"

#ifdef __cplusplus
extern "C" {
#endif

// Not in aurora; stubbed in src/cf/sdk/stubs_wpad.cpp
void KPADEnableStickCrossClamp(void);

#ifdef __cplusplus
}
#endif

#endif
