// Windows > Dump 2D layouts: writes the layouts in this frame's 2D draw list, in draw order, with each
// pane's state (visibility, alpha, global position, size, material colours and textures), to
// layout_dump.txt in the user directory (SetUserPath). For finding why a layout or pane does not show.
#include "debug_windows.h"

#include <cf/debug.h>

#include <game/mLib/m_2d.hpp>
#include <nw4r/lyt.h>

#include <cstdio>
#include <string>
#include <typeinfo>

namespace cf {
namespace debug {

namespace {

std::string sUserPath = ".";

void DumpPane(FILE* file, nw4r::lyt::Pane* pane, int depth) {
    const nw4r::math::MTX34& mtx = pane->GetGlobalMtx();
    const nw4r::lyt::Size& size = pane->GetSize();
    std::fprintf(file, "%*s%s [%s] vis=%d alpha=%u glbAlpha=%u pos=(%.1f, %.1f, %.1f) scale=(%.3f, %.3f) size=(%.1f x %.1f)\n",
                 depth * 2, "", pane->GetName(), typeid(*pane).name(), pane->IsVisible() ? 1 : 0, pane->GetAlpha(),
                 pane->GetGlbAlpha(), mtx._03, mtx._13, mtx._23, mtx._00, mtx._11, size.width, size.height);
    if (nw4r::lyt::Material* material = pane->GetMaterial()) {
        // The material colour is optional (stored only with its capability flag)
        if (material->IsMatColorCap()) {
            const nw4r::ut::Color mat = material->GetMatColor();
            std::fprintf(file, "%*s  material %s matColor=%02X%02X%02X%02X textures=%u\n", depth * 2, "",
                         material->GetName(), mat.r, mat.g, mat.b, mat.a, material->GetTextureNum());
        } else {
            std::fprintf(file, "%*s  material %s textures=%u\n", depth * 2, "", material->GetName(),
                         material->GetTextureNum());
        }
        for (u8 i = 0; i < material->GetTextureNum(); i++) {
            const nw4r::lyt::TexMap& tex = material->GetTexMapAry()[i];
            std::fprintf(file, "%*s    tex%u image=%p %ux%u fmt=%d\n", depth * 2, "", i, tex.GetImage(),
                         tex.GetWidth(), tex.GetHeight(), static_cast<int>(tex.GetTexelFormat()));
        }
    }
    auto& children = pane->GetChildList();
    for (auto it = children.GetBeginIter(); it != children.GetEndIter(); ++it) {
        DumpPane(file, &*it, depth + 1);
    }
}

struct DumpState {
    FILE* file;
    int index;
};

} // namespace

const std::string& UserPath() {
    return sUserPath;
}

void SetUserPath(const char* path) {
    if (path != nullptr) {
        sUserPath = path;
    }
}

bool DumpLayouts(std::string* path) {
    *path = sUserPath + "/layout_dump.txt";
    FILE* file = std::fopen(path->c_str(), "w");
    if (file == nullptr) {
        return false;
    }
    DumpState state = {file, 0};
    m2d::forEachDrawEntry(
        [](m2d::Base_c* entry, void* arg) {
            DumpState* state = static_cast<DumpState*>(arg);
            std::fprintf(state->file, "#%d %s priority=%u\n", state->index++, typeid(*entry).name(), entry->mPriority);
            if (m2d::Simple_c* simple = dynamic_cast<m2d::Simple_c*>(entry)) {
                const nw4r::math::VEC2& adjust = simple->mDrawInfo.GetLocationAdjustScale();
                std::fprintf(state->file, "  mPos=(%.1f, %.1f) locationAdjust=%d (%.3f)\n", simple->mPos.x,
                             simple->mPos.y, simple->mDrawInfo.IsLocationAdjust() ? 1 : 0, adjust.x);
                if (nw4r::lyt::Pane* root = simple->mLayout.GetRootPane()) {
                    DumpPane(state->file, root, 1);
                }
            }
        },
        &state);
    std::fclose(file);
    return true;
}

} // namespace debug
} // namespace cf
