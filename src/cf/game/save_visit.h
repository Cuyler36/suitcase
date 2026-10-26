// The save file's byte-order map: the visitor the generated code (tools/gen_save_swap.py, built into
// <build>/generated/cf/save_map.cpp) calls for every multi-byte value of dSaveData_c, by offset. See
// save_codec.h.
#pragma once

#include <types.h>

#include <cstddef>
#include <initializer_list>
#include <type_traits>

namespace cf {
namespace save {

class Visitor {
public:
    // count values of size bytes from offset
    virtual void Value(u32 offset, u32 size, u32 count = 1) = 0;
    // A bit-field storage unit of size bytes: its fields' widths in declaration order (MWCC packs them
    // from the most significant bit, MSVC from the least)
    virtual void Bitfield(u32 offset, u32 size, std::initializer_list<u8> widths) = 0;
    // A union whose layout depends on the data (offset, size: the union): fn visits the view the
    // struct at structBase uses, reading the struct's bytes (obj)
    using DynamicFn = void (*)(Visitor& v, u32 structBase, const u8* obj);
    virtual void Dynamic(u32 structBase, u32 offset, u32 size, DynamicFn fn) = 0;
};

// Generated for every struct in the save
template <class T>
void VisitStruct(Visitor& v, u32 base);

// For the unions in tools/gen_save_swap.py's UNION_HANDLERS (src/cf/game/save_codec.cpp)
template <class T>
void VisitUnion(Visitor& v, u32 structBase, const u8* obj);

template <class T>
void Visit(Visitor& v, u32 offset) {
    if constexpr (std::is_array_v<T>) {
        using E = std::remove_cv_t<std::remove_all_extents_t<T>>;
        constexpr size_t count = sizeof(T) / sizeof(E);
        if constexpr (std::is_arithmetic_v<E> || std::is_enum_v<E>) {
            if constexpr (sizeof(E) > 1) {
                v.Value(offset, sizeof(E), static_cast<u32>(count));
            }
        } else {
            for (size_t i = 0; i < count; i++) {
                VisitStruct<E>(v, static_cast<u32>(offset + i * sizeof(E)));
            }
        }
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        if constexpr (sizeof(T) > 1) {
            v.Value(offset, sizeof(T));
        }
    } else {
        static_assert(!std::is_pointer_v<T>, "a pointer in save data");
        VisitStruct<std::remove_cv_t<T>>(v, offset);
    }
}

constexpr size_t AlignUp(size_t value, size_t align) {
    return (value + align - 1) / align * align;
}

// The save structs' bases are their first (and only) base, at offset 0
template <class Derived, class Base>
constexpr size_t BaseOffset() {
    static_assert(std::is_base_of_v<Base, Derived>);
    return 0;
}

struct MemberSpan {
    size_t offset;
    size_t size;
    size_t align;
};

// The members, in declaration order, cover T from start to its end: each gap is padding (smaller
// than the next member's alignment). A larger gap means the generator missed a member.
template <class T>
constexpr bool IsComplete(size_t start, std::initializer_list<MemberSpan> spans) {
    size_t pos = start;
    for (const MemberSpan& s : spans) {
        if (s.offset < pos || s.offset - pos >= s.align) {
            return false;
        }
        pos = s.offset + s.size;
    }
    return sizeof(T) - pos < alignof(T);
}

} // namespace save
} // namespace cf
