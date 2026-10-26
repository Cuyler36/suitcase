// In-file pointers on a 64-bit host (the PC port). Adapted from Dusklight's include/helpers/offset_ptr.h
// (CC0).
//
// Several formats store 32-bit offsets that the loader turns into pointers in place ("relocation").
// A 64-bit pointer does not fit in the field, so on PC an OFFSET_PTR(T) field keeps a big-endian offset
// relative to the field itself and converts on access. setBase() is the relocation step: it turns an
// offset from `base` into an offset from the field and marks it relocated (bit 31), so relocating twice
// is harmless. Assigning a pointer stores it relative to the field (it must be within +-1 GiB, e.g. in
// the same buffer). On the Wii OFFSET_PTR(T) is T* and OFFSET_PTR_RAW is u32.
#ifndef PORT_OFFSET_PTR_H
#define PORT_OFFSET_PTR_H

#include <port/endian.h>

#if TARGET_PC && defined(__cplusplus)

#include <revolution/os.h>

#include <stddef.h>

extern "C++" {

struct OffsetPtr {
    BE<s32> value; // bit 31: relocated

    bool isRelocated() const {
        return (value & 0x80000000) != 0;
    }

    bool setBase(void* base) {
        if (isRelocated()) {
            return false;
        }
        ptrdiff_t diff = (u8*)this - (u8*)base;
        ptrdiff_t newDiff = value - diff;
        // Bit 31 is the flag, so the offset must fit in 31 bits
        if (newDiff < -0x40000000 || newDiff > 0x3FFFFFFF) {
            OSPanic(__FILE__, __LINE__, "OffsetPtr: offset out of range");
        }
        value = (s32)(newDiff | 0x80000000);
        return true;
    }

    // Stores `ptr` relative to this field (relocated); nullptr is stored as 0
    void set(const void* ptr) {
        if (ptr == nullptr) {
            value = 0;
            return;
        }
        ptrdiff_t diff = (const u8*)ptr - (u8*)this;
        if (diff < -0x40000000 || diff > 0x3FFFFFFF) {
            OSPanic(__FILE__, __LINE__, "OffsetPtr: pointer out of range");
        }
        value = (s32)((diff & 0x7FFFFFFF) | 0x80000000);
    }

    // The stored offset before relocation (the file's value, in host order)
    s32 raw() const {
        return value;
    }

    template <typename T>
    explicit operator T*() const {
        s32 raw = value;
        if (raw == 0) {
            return nullptr;
        }
        // Sign-extend from bit 30 (bit 31 is the flag)
        s32 offset = (raw & 0x40000000) ? (raw | (s32)0x80000000) : (raw & 0x7FFFFFFF);
        return (T*)((u8*)this + offset);
    }
};

template <typename T>
struct OffsetPtrT {
    OffsetPtr value;

    bool setBase(void* base) {
        return value.setBase(base);
    }
    bool isRelocated() const {
        return value.isRelocated();
    }

    s32 raw() const {
        return value.raw();
    }

    OffsetPtrT& operator=(T* ptr) {
        value.set(ptr);
        return *this;
    }

    T* operator->() const {
        return (T*)value;
    }
    operator T*() const {
        return (T*)value;
    }
    template <typename TOther>
    explicit operator TOther*() const {
        return (TOther*)value;
    }
};

} // extern "C++"

#define OFFSET_PTR(T) OffsetPtrT<T>
#define OFFSET_PTR_RAW OffsetPtr

#else

#define OFFSET_PTR(T) T*
#define OFFSET_PTR_RAW u32

#endif

#endif // PORT_OFFSET_PTR_H
