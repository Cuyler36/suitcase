// HOME button menu stub. The PC build does not compile HBM (lib/RVL_SDK/hbm); the menu is to
// be recreated with RmlUi later. Until then the menu reports itself closed as soon as it opens:
// dHomeButton_c::calcMenu leaves STATE_OPEN only once HBMCalc returns HBM_SELECT_HOMEBTN or
// higher, so a stub returning HBM_SELECT_NULL would hang the game on the first HOME press.
// Closing also completes requestClose (reset/power), which waits for the next HBMCalc.
#include <revolution/HBM.h>

extern "C" {

void HBMCreate(const HBMDataInfo* pHBInfo) {
    (void)pHBInfo;
}

void HBMInit(void) {}

HBMSelectBtnNum HBMCalc(const HBMControllerData* pController) {
    (void)pController;
    return HBM_SELECT_HOMEBTN;
}

void HBMDraw(void) {}

void HBMSetAdjustFlag(BOOL flag) {
    (void)flag;
}

void HBMStartBlackOut(void) {}

void HBMSetBlackOutColor(u8 red, u8 green, u8 blue) {
    (void)red;
    (void)green;
    (void)blue;
}

void HBMSetVIBlack(BOOL value) {
    (void)value;
}

void HBMCreateSound(void* soundData, void* memBuf, u32 memSize) {
    (void)soundData;
    (void)memBuf;
    (void)memSize;
}

void HBMUpdateSound(void) {}

} // extern "C"
