#pragma once
// Completion of the stubbed asynchronous NAND calls (src/cf/sdk/stubs_nand.cpp).
#include <revolution/nand.h>

namespace cf::sdk {

// Accepts an asynchronous request: `callback(result, block)` runs later on a worker thread, as the
// Wii's NAND callbacks run after the call returns (game code often enters its waiting state after
// issuing the request, so a callback from inside the call would be lost). Returns NAND_RESULT_OK.
s32 NandCompleteAsync(NANDCallback callback, NANDCommandBlock* block, s32 result);

} // namespace cf::sdk
