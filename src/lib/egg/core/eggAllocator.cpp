#include <lib/egg/core/eggAllocator.h>
#include <lib/egg/core/eggHeap.h>

namespace EGG {

Allocator::Allocator(Heap *heap, long align) : mpHeap(heap), mAlign(align) {
#if TARGET_PC
    // No heap: a static mAllocator_c built before mAllocator_c::init made the dummy heap (the RELs'
    // statics are constructed before main on PC); it sets itself up (src/dol/mLib/m_allocator.cpp)
    if (heap == nullptr) {
        return;
    }
#endif
    heap->initAllocator(this, align);
}

Allocator::~Allocator() {}

#if TARGET_PC
void *Allocator::alloc(size_t size) {
#else
void *Allocator::alloc(unsigned long size) {
#endif
    return Heap::alloc(size, mAlign, mpHeap);
}

void Allocator::free(void *block) {
    Heap::free(block, mpHeap);
}

} // namespace EGG
