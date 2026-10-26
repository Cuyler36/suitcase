#ifndef NW4R_SND_FX_BASE_H
#define NW4R_SND_FX_BASE_H

#include <types.h>

#include <nw4r/snd/snd_global.h>

#include <nw4r/ut/ut_NonCopyable.h>
#include <nw4r/ut/ut_LinkList.h>

namespace nw4r {
namespace snd {
class FxBase : private ut::NonCopyable {
  public:
    typedef ut::LinkList<FxBase, NW4R_UT_LINK_OFFSET(0x04, 0x08)> LinkList;

  public:
    virtual ~FxBase() {
    }

#if TARGET_PC
    // Not in the original binary (every effect overrides them), but MSVC emits FxBase's vtable
    // wherever an effect is constructed, so the slots need definitions.
    virtual bool StartUp() {
        return true;
    }
    virtual void Shutdown() {}
    virtual void UpdateBuffer(int, void **, u32, SampleFormat, f32, OutputMode) {}
    virtual void OnChangeOutputMode() {}
#else
    virtual bool StartUp();
    virtual void Shutdown();
    virtual void UpdateBuffer(int, void **, u32, SampleFormat, f32, OutputMode);
    virtual void OnChangeOutputMode();
#endif

  private:
    /* base ut::NonCopyable */ // size 0x00, offset 0x00
    /* vtable */               // size 0x04, offset 0x00
  public:
    ut::LinkListNode mFxLink; // size 0x08, offset 0x04
#if TARGET_PC
    // The PC list offsets (NW4R_UT_LINK_OFFSET)
    static void CheckLinkOffsets() {
        static_assert(offsetof(FxBase, mFxLink) == 0x08, "mFxLink");
    }
#endif
}; // size 0x0c
} // namespace snd
} // namespace nw4r

#endif
