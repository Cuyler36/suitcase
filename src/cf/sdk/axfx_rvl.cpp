// AXFX support for the decomp's reverbs (lib/RVL_SDK/src/axfx/AXFXReverbHiExp*.c, compiled for PC by
// cmake/CfUnits.cmake): the allocation hooks (the SDK's AXFXHooks.c allocates from an OS heap the PC
// build does not have; nw4r installs its own hooks anyway). Their interrupt guards take the interrupt
// lock (CFDisableInterruptsLocked, os_thread.cpp).
#include <private/axfx.h>
#include <revolution/axfx.h>

#include <cstdlib>

namespace {

void* DefaultAlloc(u32 size) {
    return std::malloc(size);
}

void DefaultFree(void* block) {
    std::free(block);
}

} // namespace

extern "C" {

AXFXAllocHook __AXFXAlloc = DefaultAlloc;
AXFXFreeHook __AXFXFree = DefaultFree;

void AXFXSetHooks(AXFXAllocHook alloc, AXFXFreeHook free) {
    __AXFXAlloc = alloc;
    __AXFXFree = free;
}

void AXFXGetHooks(AXFXAllocHook* alloc, AXFXFreeHook* free) {
    *alloc = __AXFXAlloc;
    *free = __AXFXFree;
}

} // extern "C"
