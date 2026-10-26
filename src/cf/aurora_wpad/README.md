# aurora WPAD/KPAD (vendored)

aurora's WPAD and KPAD implementation, from its `present-target` branch (commit `5dd8761`,
"WPAD/KPAD implementations & new binding config"), kept here until it is merged into aurora `main`.
aurora is MIT licensed (`LICENSE`).

The submodule tracks aurora `main`, which this branch is behind (it lacks GX fixes and the GX
stubs the port relies on), so only the WPAD/KPAD pieces are taken.

## Files

From aurora `5dd8761`, unchanged:

- `include/revolution/wpad.h`, `include/aurora/wpad.h`, `include/aurora/wpad.hpp`
- `lib/revolution/kpad.cpp`
- `lib/revolution/wpad/wpad.cpp`, `internal.hpp`, `backend.hpp`

From aurora `5dd8761`, changed:

- `include/revolution/kpad.h`: the `Vec2` typedef is behind `REVOLUTION_VEC2_DEFINED`, which the
  revolution layer's `mtx.h` also uses.

Ours, standing in for parts of the branch's input rework that aurora `main` does not have:

- `lib/revolution/wpad/provider.cpp`: replaces the branch's provider (bindings, profiles, motion
  sensors) with mouse, keyboard and gamepads on channel 0 through aurora `main`'s input layers. The
  mouse is the IR pointer and A/B; the keys follow the branch's default keyboard bindings. Any SDL
  gamepad (GameCube controllers on a Wii U adapter, Classic Controllers on adapters) is the Wii
  Remote and Nunchuk: left stick the Nunchuk's, right stick the pointer. GameCube controllers have
  their own layout (L/R Z, Z 1, X +, Y/Start -).
- `include/suitcase/wpad_host.hpp`: the provider's settings (stick gain, dead zone, trigger point,
  pointer speed; set from `src/cf/config`) and its left-stick reading for the debug UI.
- `lib/input/port.hpp`: declares `aurora::pad::detail::blocked()`, which aurora `main` keeps private
  (always false here).

The branch samples from aurora's input pump. aurora `main` has no such hook, so
`VIWaitForRetrace` (`src/cf/sdk/rvl_misc.cpp`) calls `aurora_wpad_update()` once per frame.

## Removing it

Once aurora `main` has WPAD/KPAD:

1. Update the submodule, delete this directory and the `cf_aurora_wpad` target in `CMakeLists.txt`,
   and link `aurora::wpad` and `aurora::kpad` instead.
2. In `tools/gen_pc_revolution_layer.py`, point the `wpad.h`/`kpad.h` wrappers at aurora's headers.
   Keep their additions (the decomp's names aurora lacks) and the `Vec2` guard, or upstream it.
3. Drop the `aurora_wpad_update()` call in `VIWaitForRetrace` if aurora samples from its own pump.
