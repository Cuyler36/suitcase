// "Physical" addresses on PC. The SDK hands 32-bit physical addresses to the DSP: nw4r snd builds AX
// sample addresses from OSCachedToPhysical and reads samples back through OSPhysicalToCached.
// aurora's versions only cover MEM1, so src/cf/include/revolution/os.h routes the game's calls here:
//   0x00000000  MEM1 (aurora's arena, as aurora maps it)
//   0x10000000  MEM2 (os_rvl.cpp)
//   0x20000000  any other host memory (static data such as nw4r's zero buffer), in 1 MiB windows
//               assigned on first use
// Everything stays below 0x80000000, so ADPCM nibble addresses (twice the byte address) fit 32 bits.
#include <revolution/types.h>

#include <borealis/log_c.h>

#include <atomic>
#include <cstddef>
#include <mutex>

// aurora's MEM1 (lib/dolphin/os/OSMemory.cpp)
extern void* MEM1Start;
extern void* MEM1End;

namespace cf {
namespace sdk {
void GetMem2Range(u8** start, size_t* size); // os_rvl.cpp
} // namespace sdk
} // namespace cf

namespace {

constexpr u32 MEM2_PHYS = 0x10000000;
constexpr u32 WINDOW_PHYS = 0x20000000;
constexpr u32 WINDOW_SHIFT = 20;
constexpr uintptr_t WINDOW_MASK = (uintptr_t(1) << WINDOW_SHIFT) - 1;
constexpr u32 WINDOW_MAX = (0x80000000u - WINDOW_PHYS) >> WINDOW_SHIFT;

struct Range {
    uintptr_t start;
    uintptr_t size;

    bool Contains(uintptr_t p) const {
        return p - start < size;
    }
};

Range GetMem1() {
    uintptr_t start = reinterpret_cast<uintptr_t>(MEM1Start);
    return {start, reinterpret_cast<uintptr_t>(MEM1End) - start};
}

const Range& GetMem2() {
    static const Range sRange = [] {
        u8* start;
        size_t size;
        cf::sdk::GetMem2Range(&start, &size);
        return Range{reinterpret_cast<uintptr_t>(start), size};
    }();
    return sRange;
}

// Host block numbers (address >> WINDOW_SHIFT) of the windows, in physical order. Append-only:
// readers take sWindowNum first.
std::atomic<uintptr_t> sWindows[WINDOW_MAX];
std::atomic<u32> sWindowNum{0};
std::mutex sWindowMutex;

s32 FindWindow(uintptr_t block, u32 num) {
    for (u32 i = 0; i < num; i++) {
        if (sWindows[i].load(std::memory_order_relaxed) == block) {
            return static_cast<s32>(i);
        }
    }
    return -1;
}

s32 MapWindow(uintptr_t block) {
    std::lock_guard lock(sWindowMutex);
    u32 num = sWindowNum.load(std::memory_order_relaxed);
    s32 index = FindWindow(block, num);
    if (index >= 0) {
        return index;
    }
    // The block and the one after it, so a buffer that crosses into the next block stays
    // contiguous in physical addresses
    u32 add = FindWindow(block + 1, num) < 0 ? 2 : 1;
    if (num + add > WINDOW_MAX) {
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::dsp", "out of physical address windows");
        return -1;
    }
    sWindows[num].store(block, std::memory_order_relaxed);
    if (add == 2) {
        sWindows[num + 1].store(block + 1, std::memory_order_relaxed);
    }
    sWindowNum.store(num + add, std::memory_order_release);
    return static_cast<s32>(num);
}

} // namespace

extern "C" {

u32 CFCachedToPhysical(const void* caddr) {
    uintptr_t p = reinterpret_cast<uintptr_t>(caddr);
    Range mem1 = GetMem1();
    if (mem1.Contains(p)) {
        return static_cast<u32>(p - mem1.start);
    }
    const Range& mem2 = GetMem2();
    if (mem2.Contains(p)) {
        return MEM2_PHYS + static_cast<u32>(p - mem2.start);
    }
    uintptr_t block = p >> WINDOW_SHIFT;
    s32 index = FindWindow(block, sWindowNum.load(std::memory_order_acquire));
    if (index < 0) {
        index = MapWindow(block);
        if (index < 0) {
            return 0;
        }
    }
    return WINDOW_PHYS + (static_cast<u32>(index) << WINDOW_SHIFT) + static_cast<u32>(p & WINDOW_MASK);
}

void* CFPhysicalToCached(u32 paddr) {
    if (paddr < MEM2_PHYS) {
        return reinterpret_cast<void*>(GetMem1().start + paddr);
    }
    if (paddr < WINDOW_PHYS) {
        return reinterpret_cast<void*>(GetMem2().start + (paddr - MEM2_PHYS));
    }
    u32 index = (paddr - WINDOW_PHYS) >> WINDOW_SHIFT;
    if (index >= sWindowNum.load(std::memory_order_acquire)) {
        return nullptr;
    }
    uintptr_t block = sWindows[index].load(std::memory_order_relaxed);
    return reinterpret_cast<void*>((block << WINDOW_SHIFT) + (paddr & WINDOW_MASK));
}

} // extern "C"
