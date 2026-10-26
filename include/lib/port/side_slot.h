// Run-time data that does not fit the space a file reserves for it (the PC port). Some formats reserve
// a fixed-size slot that the game fills at run time: nw4r g3d stores bound texture pointers (4 bytes) and
// GXTexObj/GXTlutObj (32/12 bytes) inside material data. On PC a pointer is 8 bytes and aurora's GX
// objects are larger, so the slot keeps the file's size and the real value lives in a side table keyed
// by the slot's address (src/cf/sdk/side_slot.cpp). On the Wii none of this is used.
//
// A stale entry (left by data that was freed) is harmless: the game writes a slot before reading it.
// A plain memcpy of a slot does not copy its side value; code that copies slots copies the values.
#ifndef PORT_SIDE_SLOT_H
#define PORT_SIDE_SLOT_H

#if TARGET_PC && defined(__cplusplus)

#include <revolution/types.h>

#include <new>
#include <stddef.h>

namespace port {

// The pointer stored for `key`, or nullptr
void* SideSlotGetPtr(const void* key);
void SideSlotSetPtr(const void* key, void* value);
// The object of `size` bytes kept for `key`, created zeroed on first use
void* SideSlotObject(const void* key, size_t size);

} // namespace port

extern "C++" {

// A 4-byte pointer field: 0 is null, otherwise the pointer is in the side table
template <typename T>
struct SidePtr {
    u32 tag;

    operator T*() const {
        return tag != 0 ? static_cast<T*>(port::SideSlotGetPtr(this)) : nullptr;
    }
    T* operator->() const {
        return *this;
    }
    SidePtr& operator=(T* value) {
        tag = value != nullptr ? 1 : 0;
        port::SideSlotSetPtr(this, const_cast<void*>(static_cast<const void*>(value)));
        return *this;
    }
};

// A slot of the file's `SlotSize` bytes holding a T that lives in the side table
template <typename T, size_t SlotSize>
struct SideObj {
    u8 slot[SlotSize];

    T& get() {
        return *static_cast<T*>(port::SideSlotObject(this, sizeof(T)));
    }
    const T& get() const {
        return *static_cast<const T*>(port::SideSlotObject(this, sizeof(T)));
    }
};

} // extern "C++"

#endif

#endif // PORT_SIDE_SLOT_H
