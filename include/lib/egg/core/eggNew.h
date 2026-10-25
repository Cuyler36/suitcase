#pragma once

// The EGG_NEW family: allocation through EGG's global heap operators (see eggHeap.h). Kept
// separate from eggHeap.h so code outside EGG (nw4r, the keyboard library) can use it.

#if TARGET_PC
/*
 * On the Wii, EGG replaces the global operator new/delete so every allocation goes to its heaps.
 * On PC that would also capture the allocations of aurora, borealis and the C++ library, so the
 * game's operators instead take an EGG::HeapToken and game code allocates through the EGG_NEW
 * family below (adapted from Dusklight's JKR_NEW, JKRHeap.h). Without TARGET_PC the macros are
 * plain new/delete.
 *
 * Only expressions that resolve to these global operators use the macros. Placement new into a
 * buffer, and classes with their own operator new/delete, keep the plain expressions.
 */
#include <cstddef>
#include <new>
#include <type_traits>

namespace EGG {
class Heap;
enum class HeapToken { Dummy };
} // namespace EGG

void *operator new(size_t size, EGG::HeapToken) noexcept;
void *operator new(size_t size, EGG::HeapToken, int align) noexcept;
void *operator new(size_t size, EGG::HeapToken, EGG::Heap *heap, int align) noexcept;
// Only to catch EGG_NEW used with []: arrays go through EGG_NEW_ARRAY
void *operator new[](size_t size, EGG::HeapToken) noexcept;
void *operator new[](size_t size, EGG::HeapToken, int align) noexcept;
void *operator new[](size_t size, EGG::HeapToken, EGG::Heap *heap, int align) noexcept;
void operator delete(void *p, EGG::HeapToken) noexcept;
void operator delete[](void *p, EGG::HeapToken) noexcept;

#define EGG_NEW new (EGG::HeapToken::Dummy)
#define EGG_NEW_ARGS(...) new (EGG::HeapToken::Dummy, __VA_ARGS__)
#define EGG_NEW_ARRAY(type, count) ::EGG::detail::NewArray<type>(count)
#define EGG_NEW_ARRAY_ARGS(type, count, ...) ::EGG::detail::NewArray<type>(count, __VA_ARGS__)
#define EGG_DELETE(expr) ::EGG::detail::Delete(expr)
#define EGG_DELETE_ARRAY(expr) ::EGG::detail::DeleteArray(expr)

namespace EGG {
namespace detail {

template <typename T>
void Delete(T *ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    // The complete object's address, for deletes through a base class that is not the first base
    void *block = ptr;
    if constexpr (std::is_polymorphic_v<T>) {
        block = dynamic_cast<void *>(ptr);
    }
    ptr->~T();

    if constexpr (requires { T::operator delete(ptr, HeapToken::Dummy); }) {
        T::operator delete(ptr, HeapToken::Dummy);
    } else if constexpr (requires { T::operator delete(ptr, sizeof(T), HeapToken::Dummy); }) {
        T::operator delete(ptr, sizeof(T), HeapToken::Dummy);
    } else if constexpr (requires { T::operator delete(ptr); }) {
        T::operator delete(ptr);
    } else if constexpr (requires { T::operator delete(ptr, sizeof(T)); }) {
        T::operator delete(ptr, sizeof(T));
    } else {
        ::operator delete(block, HeapToken::Dummy);
    }
}

template <>
inline void Delete(void *ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    ::operator delete(ptr, HeapToken::Dummy);
}

// Alignment of an EGG_NEW_ARRAY(_ARGS) allocation, from the same arguments the operators take
inline size_t ArrayAlign() {
    return alignof(max_align_t);
}
inline size_t ArrayAlign(int align) {
    return align < 0 ? -align : align;
}
inline size_t ArrayAlign(Heap *, int align) {
    return ArrayAlign(align);
}

/*
 * C++ cannot pair a placement new[] with delete[], so arrays of types with a destructor keep their
 * element count in a prefix, as in Dusklight. Unlike Dusklight's version, the prefix also records
 * its own size and is padded to the allocation's alignment, so custom alignments are supported.
 */
template <typename T, typename... Args>
T *NewArray(size_t count, Args... args) noexcept {
    size_t prefix = 0;
    if constexpr (!std::is_trivially_destructible_v<T>) {
        size_t align = ArrayAlign(args...);
        if (align < alignof(T)) {
            align = alignof(T);
        }
        prefix = (2 * sizeof(size_t) + align - 1) & ~(align - 1);
    }

    unsigned char *mem = static_cast<unsigned char *>(::operator new(prefix + count * sizeof(T), HeapToken::Dummy, args...));
    if (mem == nullptr) {
        return nullptr;
    }

    T *data = reinterpret_cast<T *>(mem + prefix);
    if constexpr (!std::is_trivially_destructible_v<T>) {
        reinterpret_cast<size_t *>(data)[-1] = count;
        reinterpret_cast<size_t *>(data)[-2] = prefix;
    }
    if constexpr (!std::is_trivially_default_constructible_v<T>) {
        for (size_t i = 0; i < count; i++) {
            new (data + i) T; // default-initialized, as new T[count]
        }
    }
    return data;
}

template <typename T>
void DeleteArray(T *ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }

    if constexpr (!std::is_trivially_destructible_v<T>) {
        size_t count = reinterpret_cast<size_t *>(ptr)[-1];
        size_t prefix = reinterpret_cast<size_t *>(ptr)[-2];
        for (size_t i = 0; i < count; i++) {
            ptr[i].~T();
        }
        ::operator delete(reinterpret_cast<unsigned char *>(ptr) - prefix, HeapToken::Dummy);
    } else {
        ::operator delete(ptr, HeapToken::Dummy);
    }
}

template <>
inline void DeleteArray(void *ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    ::operator delete(ptr, HeapToken::Dummy);
}

} // namespace detail
} // namespace EGG
#else
#define EGG_NEW new
#define EGG_NEW_ARGS(...) new (__VA_ARGS__)
#define EGG_NEW_ARRAY(type, count) new type[count]
#define EGG_NEW_ARRAY_ARGS(type, count, ...) new (__VA_ARGS__) type[count]
#define EGG_DELETE(expr) delete (expr)
#define EGG_DELETE_ARRAY(expr) delete[] (expr)
#endif
