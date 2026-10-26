#include <nw4r/g3d.h>
#include <nw4r/math.h>

namespace nw4r {
namespace g3d {

NW4R_G3D_RTTI_DEF(AnmObj);

#if defined(VERSION_RSPE01_01)
f32 FrameCtrl::smBaseUpdateRate = 1.0f;
#endif

f32 PlayPolicy_Onetime(f32 start, f32 end, f32 frame) {
#pragma unused(start)
#pragma unused(end)

    return frame;
}

f32 PlayPolicy_Loop(f32 start, f32 end, f32 frame) {
    f32 length = end - start;

    if (frame >= 0.0f) {
        return math::FMod(frame, length);
    }

    f32 offset = math::FMod(frame + length, length);
    return offset + math::FSelect(offset, 0.0f, length);
}

void AnmObj::SetAnmFlag(AnmFlag flag, bool value) {
    if (value) {
        mFlags |= flag;
    } else {
        mFlags &= ~flag;
    }
}

bool AnmObj::TestAnmFlag(AnmFlag flag) const {
    return mFlags & flag;
}

#if TARGET_PC
// Not in the original binary (every animation object overrides it), but MSVC emits AnmObj's vtable
// wherever a derived object is constructed, so the slot needs a definition.
void AnmObj::Release() {}
#endif

} // namespace g3d
} // namespace nw4r
