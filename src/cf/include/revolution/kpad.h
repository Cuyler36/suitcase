#ifndef CF_REVOLUTION_KPAD_H
#define CF_REVOLUTION_KPAD_H
// PC: aurora's KPAD (vendored in src/cf/aurora_wpad until aurora main has it), plus what the decomp's
// include/lib/revolution/kpad.h has that aurora does not.
#include <revolution/mtx.h>
#include <revolution/wpad.h>
#include "../../aurora_wpad/include/revolution/kpad.h"

#ifdef __cplusplus
extern "C" {
#endif

// Not in aurora; stubbed in src/cf/sdk/stubs_wpad.cpp
void KPADEnableStickCrossClamp(void);

#ifdef __cplusplus
}
#endif

#endif
