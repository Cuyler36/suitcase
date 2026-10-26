// The offset of a base class inside a derived class (the PC port). MWCC puts a class's vtable pointer
// after its non-polymorphic bases, so such a base is at offset 0 on the Wii; MSVC (and the Itanium ABI)
// put the vtable pointer first. Code that hard-codes 0 for, e.g., an nw4r::ut::Link base's list offset
// uses PORT_BASE_OFFSET on PC instead.
#ifndef PORT_BASE_OFFSET_H
#define PORT_BASE_OFFSET_H

#ifdef __cplusplus

#include <stdint.h>

// A non-null dummy address, so static_cast adjusts the pointer (it would keep a null one null)
#define PORT_BASE_OFFSET(Derived, Base)                                                            \
    ((unsigned)(reinterpret_cast<uintptr_t>(static_cast<Base*>(reinterpret_cast<Derived*>(0x1000))) - \
                0x1000))

#endif

#endif // PORT_BASE_OFFSET_H
