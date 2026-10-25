# PC include overrides

Placed first on the include path of every decomp target (`cmake/CfUnits.cmake`). Headers here
replace decomp headers that only make sense for the Wii toolchain:

- `MSL/...`, `lib/MSL/...`: game code includes some MSL headers directly; on PC they forward to the
  host C/C++ library.
- `extras.h`: MSL's non-standard string functions (`stricmp`, `strnicmp`), as in Dusklight.

The `revolution/` header layer (aurora-backed SDK headers) will also live here.
