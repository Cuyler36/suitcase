#pragma once

#include <game/framework/f_base.hpp>

// Game-side loader and creation helpers, named after their NSMBW counterparts.
// No separate constructor, virtual overrides, or instance fields are identified.
class dBase_c : public fBase_c {
public:
    dBase_c() {}
    ~dBase_c() {}

    static int loadAsyncCallback();
    static void unloadCallback();
    static void initLoader();
    static fBase_c *createBase(ProfileName profile, fBase_c *parent, fBaseParam param, u8 group);
    static fBase_c *createRoot(ProfileName profile, fBaseParam param, u8 group);
};
