// Runs the REL units' static initializers, which src/cf/include/cf/rel_static_init.h moves out of the
// C runtime's start-up into the .cfrel$m section. The linker sorts .cfrel$a, $m and $z by name, so the
// two markers here bound them.
#include <cf/rel.h>

namespace {

using Initializer = void(__cdecl*)();

#if defined(_MSC_VER)
#pragma section(".cfrel$a", read)
__declspec(allocate(".cfrel$a")) const Initializer sRelInitStart = nullptr;
#pragma section(".cfrel$z", read)
__declspec(allocate(".cfrel$z")) const Initializer sRelInitEnd = nullptr;
#endif

bool sRan = false;

} // namespace

namespace cf {

void RunRelStaticInitializers() {
    if (sRan) {
        return;
    }
    sRan = true;
#if defined(_MSC_VER)
    // The linker may pad the section with zeros
    for (const Initializer* init = &sRelInitStart + 1; init < &sRelInitEnd; ++init) {
        if (*init != nullptr) {
            (*init)();
        }
    }
#endif
}

} // namespace cf
