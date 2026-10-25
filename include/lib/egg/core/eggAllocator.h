#pragma once
#include <types.h>
#include <lib/egg/core/eggHeap.h>
#include <revolution/mem.h>

namespace EGG {
class Allocator : public MEMAllocator {
public:
    Allocator(Heap *heap, long align);

    virtual ~Allocator();
#if TARGET_PC
    // size_t, as the overriders declare it (unsigned long is not size_t on 64-bit hosts)
    virtual void *alloc(size_t size);
#else
    virtual void *alloc(unsigned long size);
#endif
    virtual void free(void *block);

public:
    Heap *mpHeap;  // 0x14
    int mAlign;    // 0x18
};
} // namespace EGG
