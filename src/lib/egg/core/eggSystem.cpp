#include <lib/egg/core/eggSystem.h>

#include <lib/egg/core/eggController.h>
#include <lib/egg/core/eggExpHeap.h>
#include <revolution/os.h>
#include <revolution/os/OSBootInfo.h>

namespace EGG {

ConfigurationData *BaseSystem::mConfigData;

void ConfigurationData::initMemory() {
    void *mem1Lo = OSGetMEM1ArenaLo();
    void *mem1Hi = OSGetMEM1ArenaHi();
    void *mem2Lo = OSGetMEM2ArenaLo();
    void *mem2Hi = OSGetMEM2ArenaHi();

    void *mem1Arena = OSInitAlloc(mem1Lo, mem1Hi, 2);
    void *mem2Arena = OSInitAlloc(mem2Lo, mem2Hi, 2);

    mem1Lo = (void *)(((uintptr_t)mem1Arena + 31) & ~31);
    mem2Lo = (void *)(((uintptr_t)mem2Arena + 31) & ~31);

    mem1Arena = (void *)((uintptr_t)mem1Hi & ~31);
    mem2Arena = (void *)((uintptr_t)mem2Hi & ~31);

    OSBootInfo *bootInfo = (OSBootInfo *)OSPhysicalToCached(0);

    mCodeStart = bootInfo;
    mCodeEnd = mem1Lo;

    mMem1Start = mem1Lo;
    mMem1End = mem1Arena;

    mMem2Start = mem2Lo;
    mMem2End = mem2Arena;

    mMemorySize = bootInfo->memorySize;

    OSSetMEM1ArenaLo(mem1Arena);
    OSSetMEM1ArenaHi(mem1Arena);

    OSSetMEM2ArenaLo(mem2Arena);
    OSSetMEM2ArenaHi(mem2Arena);

    Heap::initialize();

    u32 heapSizeMem1 = (uintptr_t)mMem1End - (uintptr_t)mMem1Start;
    u32 heapSizeMem2 = (uintptr_t)mMem2End - (uintptr_t)mMem2Start;
    u32 heapSizeDebug;

#if TARGET_PC
    // PC: MEM2 is larger than a retail Wii's to fit 64-bit data; keep the retail layout (no debug heap)
    if (false) {
#else
    if (heapSizeMem2 >= 0x4000000) {
#endif
        heapSizeDebug = 0x4000000;
        heapSizeMem2 = (heapSizeMem2 - heapSizeDebug) & ~31;
    } else {
        heapSizeDebug = 0;
    }

    mem1Lo = mMem1Start;
    mem2Lo = mMem2Start;
    void *memDebugStart = (u8 *)mem2Lo + heapSizeMem2;

    mRootHeapMem1 = ExpHeap::create(mem1Lo, heapSizeMem1, 0);
    mRootHeapMem1->setName("EGGRootMEM1");

    mRootHeapMem2 = ExpHeap::create(mem2Lo, heapSizeMem2, 0);
    mRootHeapMem2->setName("EGGRootMEM2");

    if (heapSizeDebug > 0) {
        mRootHeapDebug = ExpHeap::create(memDebugStart, heapSizeDebug, 0);
        mRootHeapDebug->setName("EGGRootDebug");
    } else {
        mRootHeapDebug = NULL;
    }

    mSystemHeap = ExpHeap::create(mSysHeapSize, mRootHeapMem1, 0);
    mSystemHeap->setName("EGGSystem");
    mSystemHeap->becomeCurrentHeap();
}

void ConfigurationData::run() {
    CoreControllerMgr *controllerMgr = CoreControllerMgr::instance();
    GCControllerMgr *gcMgr = GCControllerMgr::instance();

    while (true) {
        getDisplay()->beginFrame();
        onBeginFrame();
        getPerfView()->measureBeginFrame();

        controllerMgr->beginFrame();
        gcMgr->calc();

        getDisplay()->beginRender();
        getPerfView()->measureBeginRender();

        getSceneMgr()->draw();
        getPerfView()->draw();

        getPerfView()->measureEndRender();
        getDisplay()->endRender();

        getSceneMgr()->calc();

        controllerMgr->endFrame();

        if (getAudioMgr() != NULL) {
            getAudioMgr()->calc();
        }

        getDisplay()->endFrame();
        onEndFrame();
        getPerfView()->measureEndFrame();
    }
}

} // namespace EGG
