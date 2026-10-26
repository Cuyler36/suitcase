// NAND functions the PC build implements rather than stubs (the rest: stubs_nand.cpp).
#include <revolution/nand.h>

extern "C" {

// The command block carries the caller's context to its callback
void NANDSetUserData(NANDCommandBlock* block, void* data) {
    block->userData = data;
}

void* NANDGetUserData(NANDCommandBlock* block) {
    return block->userData;
}

} // extern "C"
