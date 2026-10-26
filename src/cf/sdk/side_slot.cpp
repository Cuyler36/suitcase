// The side table behind include/lib/port/side_slot.h.
#include <port/side_slot.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace port {
namespace {

struct Entry {
    void* ptr = nullptr;
    std::unique_ptr<u8[]> object;
    size_t objectSize = 0;
};

std::mutex& Mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<const void*, Entry>& Table() {
    static std::unordered_map<const void*, Entry> t;
    return t;
}

} // namespace

void* SideSlotGetPtr(const void* key) {
    std::lock_guard lock{Mutex()};
    auto it = Table().find(key);
    return it != Table().end() ? it->second.ptr : nullptr;
}

void SideSlotSetPtr(const void* key, void* value) {
    std::lock_guard lock{Mutex()};
    Table()[key].ptr = value;
}

void* SideSlotObject(const void* key, size_t size) {
    std::lock_guard lock{Mutex()};
    Entry& e = Table()[key];
    if (e.objectSize < size) {
        e.object = std::make_unique<u8[]>(size);
        std::memset(e.object.get(), 0, size);
        e.objectSize = size;
    }
    // Entries are never erased, so the object stays put (unordered_map nodes do not move)
    return e.object.get();
}

} // namespace port
