#pragma once
// Helpers for the generated SDK stubs (tools/gen_pc_sdk_stubs.py).
#include <type_traits>

namespace cf::sdk {

// A failure result for a stubbed SDK call: -1 for signed integers and enums with a signed
// underlying type (the SDKs' error codes are negative), otherwise zero / NULL.
template <typename T>
T StubFail() {
    if constexpr (std::is_enum_v<T>) {
        if constexpr (std::is_signed_v<std::underlying_type_t<T>>) {
            return static_cast<T>(-1);
        } else {
            return T{};
        }
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        return static_cast<T>(-1);
    } else {
        return T{};
    }
}

} // namespace cf::sdk
