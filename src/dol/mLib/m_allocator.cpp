#include <game/mLib/m_allocator_dummy_heap.hpp>
#include <game/mLib/m_allocator.hpp>
#include <game/mLib/m_heap.hpp>

mAllocatorDummyHeap_c *mAllocatorDummyHeap_c::m_instance;
MEMiHeapHead *mAllocatorDummyHeap_c::m_heapHead;

// 802AE4F0
void *mAllocatorDummyHeap_c::AllocatorAllocForDummyHeap(MEMAllocator *, u32) {
    return nullptr;
}

// 802AE4F8
void mAllocatorDummyHeap_c::AllocatorFreeForDummyHeap(MEMAllocator *, void *) {}

// 802AE4FC
mAllocatorDummyHeap_c::mAllocatorDummyHeap_c() : EGG::Heap(m_heapHead) {
    m_instance = this;
}

#if TARGET_PC
static const MEMAllocatorFunc sDummyAllocatorFunc = {mAllocatorDummyHeap_c::AllocatorAllocForDummyHeap,
                                                     mAllocatorDummyHeap_c::AllocatorFreeForDummyHeap};

void mAllocatorDummyHeap_c::initDummyAllocator(EGG::Allocator *allocator, long alignment) {
    allocator->func = &sDummyAllocatorFunc;
    allocator->heap = nullptr;
    allocator->heapParam1 = alignment;
    allocator->heapParam2 = 0;
}
#endif

// 802AE540
void mAllocatorDummyHeap_c::initAllocator(EGG::Allocator *allocator, long alignment) {
#if TARGET_PC
    initDummyAllocator(allocator, alignment);
    allocator->heap = mHeapHandle;
#else
    static const MEMAllocatorFunc sAllocatorFunc = {AllocatorAllocForDummyHeap,
                                                    AllocatorFreeForDummyHeap};
    allocator->func = &sAllocatorFunc;
    allocator->heap = mHeapHandle;
    allocator->heapParam1 = alignment;
    allocator->heapParam2 = 0;
#endif
}

// 802AE560
mAllocatorDummyHeap_c *mAllocatorDummyHeap_c::getInstance() {
    return m_instance;
}

// 802AE568
mAllocator_c::mAllocator_c() : EGG::Allocator(mAllocatorDummyHeap_c::getInstance(), 4) {
#if TARGET_PC
    if (mpHeap == nullptr) {
        mAllocatorDummyHeap_c::initDummyAllocator(this, 4);
    }
#endif
}

// 802AE5B4
mAllocator_c::~mAllocator_c() {}

// 802AE60C
bool mAllocator_c::attach(EGG::Heap *heap, int alignment) {
    if (heap == nullptr) {
        heap = EGG::Heap::getCurrentHeap();
    }
    mAlign = alignment;
    mpHeap = heap;
    heap->initAllocator(this, alignment);
    return true;
}

// 802AE65C
void *mAllocator_c::alloc(size_t size) {
#if TARGET_PC
    // Unattached and made before the dummy heap (initDummyAllocator): as the dummy heap does. EGG's
    // Heap::alloc would take a null heap as the current heap.
    if (mpHeap == nullptr) {
        return nullptr;
    }
#endif
    return EGG::Allocator::alloc(size);
}

// 802AE660
void mAllocator_c::free(void *ptr) {
#if TARGET_PC
    if (mpHeap == nullptr) {
        return;
    }
#endif
    EGG::Allocator::free(ptr);
}

// 802AE664
void mAllocator_c::init(EGG::Heap *heap) {
    EGG_NEW_ARGS(heap, 4) mAllocatorDummyHeap_c();
}

// 802AE69C
bool mHeapAllocator_c::createExpHeap(size_t size, EGG::Heap *parent, const char *name, ulong align) {
    destroyHeap();
    EGG::ExpHeap *heap = EGG::ExpHeap::create(size, parent, 0);
    if (heap == nullptr) {
        return false;
    }
    attach(heap, align);
    if (name != nullptr) {
        heap->setName(name);
    }
    return true;
}

// 802AE724
void mHeapAllocator_c::destroyHeap() {
#if TARGET_PC
    if (!mAllocatorDummyHeap_c::isDummy(mpHeap)) {
#else
    if (mpHeap != mAllocatorDummyHeap_c::getInstance()) {
#endif
        mpHeap->destroy();
        mpHeap = mAllocatorDummyHeap_c::getInstance();
    }
}

// 802AE780
mAllocatorDummyHeap_c::~mAllocatorDummyHeap_c() {}
