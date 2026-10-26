// Force-included first into every REL unit (src/<module>NP/, cmake/CfUnits.cmake). Do not include it
// anywhere else.
//
// On the Wii a REL's static constructors run in its prolog, when d_dylink links it, after the game has
// set up its heaps; some depend on that (e.g. mAllocator_c takes the dummy heap mAllocator_c::init
// creates in dSys::init). The PC build links the RELs statically, so their initializers would run
// before main. This puts them in a section the C runtime does not run; cf::RunRelStaticInitializers
// (src/cf/game/rel_static_init.cpp) runs them when d_dylink starts.
#pragma once

#if defined(__cplusplus) && defined(_MSC_VER)
#pragma warning(disable : 4075) // initializers put in an unrecognized initialization area
#pragma init_seg(".cfrel$m")
#endif
