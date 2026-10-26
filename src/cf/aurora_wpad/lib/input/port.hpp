// suitcase: stands in for aurora's lib/input/port.hpp from the present-target branch, which needs
// that branch's input rework. wpad.cpp only uses pad::detail::blocked() from it.
#pragma once

#include <aurora/input.hpp>
#include <aurora/pad.hpp>

namespace aurora::pad::detail {

// Whether PAD input is blocked (e.g. by a menu). aurora main keeps this private; defined in
// provider.cpp, always false until aurora exposes it.
bool blocked();

} // namespace aurora::pad::detail
