// Interpolated models for frames between ticks (src/cf/include/cf/interp.h).
#include <cf/interp.h>

#include <game/mLib/m_2d.hpp>
#include <game/mLib/m_3d/global.hpp>
#include <nw4r/g3d.h>
#include <nw4r/lyt.h>

#include <cmath>
#include <unordered_map>
#include <vector>

namespace cf {
namespace interp {

namespace {

// A model that moves further in a tick is not blended (a warp, a respawn)
constexpr f32 kSnapDistance = 300.0f;
// The same for a layout's pane or the layout itself, in layout units (a page switching, a menu jumping)
constexpr f32 kLayoutSnapDistance = 120.0f;

struct Quat {
    f32 x, y, z, w;
};

// A matrix as translation, scale and rotation
struct Pose {
    f32 t[3];
    f32 s[3];
    Quat q;
};

Quat FromRotation(const f32 r[3][3]) {
    Quat q;
    const f32 trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f) {
        const f32 s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r[2][1] - r[1][2]) / s;
        q.y = (r[0][2] - r[2][0]) / s;
        q.z = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const f32 s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q.w = (r[2][1] - r[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const f32 s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q.w = (r[0][2] - r[2][0]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (r[1][2] + r[2][1]) / s;
    } else {
        const f32 s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q.w = (r[1][0] - r[0][1]) / s;
        q.x = (r[0][2] + r[2][0]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

Pose Decompose(const nw4r::math::MTX34& m) {
    Pose pose;
    f32 r[3][3];
    for (int col = 0; col < 3; col++) {
        const f32 len = std::sqrt(m.m[0][col] * m.m[0][col] + m.m[1][col] * m.m[1][col] + m.m[2][col] * m.m[2][col]);
        pose.s[col] = len;
        for (int row = 0; row < 3; row++) {
            r[row][col] = len > 0.0f ? m.m[row][col] / len : (row == col ? 1.0f : 0.0f);
        }
    }
    // A mirrored matrix: one negative scale, so the rotation is proper
    const f32 det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                    r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                    r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.0f) {
        pose.s[0] = -pose.s[0];
        for (int row = 0; row < 3; row++) {
            r[row][0] = -r[row][0];
        }
    }
    pose.q = FromRotation(r);
    for (int row = 0; row < 3; row++) {
        pose.t[row] = m.m[row][3];
    }
    return pose;
}

void Compose(const Pose& pose, nw4r::math::MTX34& m) {
    const Quat& q = pose.q;
    const f32 r[3][3] = {
        {1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y - q.z * q.w), 2 * (q.x * q.z + q.y * q.w)},
        {2 * (q.x * q.y + q.z * q.w), 1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z - q.x * q.w)},
        {2 * (q.x * q.z - q.y * q.w), 2 * (q.y * q.z + q.x * q.w), 1 - 2 * (q.x * q.x + q.y * q.y)},
    };
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            m.m[row][col] = r[row][col] * pose.s[col];
        }
        m.m[row][3] = pose.t[row];
    }
}

// Normalized lerp on the shorter arc: the rotations in one tick are small
Quat Nlerp(Quat a, const Quat& b, f32 t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
        a = {-a.x, -a.y, -a.z, -a.w};
    }
    Quat q = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    const f32 len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 0.0f) {
        q = {q.x / len, q.y / len, q.z / len, q.w / len};
    }
    return q;
}

void BlendLinear(const nw4r::math::MTX34& from, nw4r::math::MTX34& to, f32 t);

// Quaternion decomposition cannot represent shear or recover rotation from a collapsed axis.
bool NeedsLinearBlend(const nw4r::math::MTX34& m) {
    f32 lengthSq[3] = {};
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            lengthSq[col] += m.m[row][col] * m.m[row][col];
        }
        if (lengthSq[col] <= 1.0e-12f) {
            return true;
        }
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            f32 dot = 0.0f;
            for (int row = 0; row < 3; ++row) {
                dot += m.m[row][a] * m.m[row][b];
            }
            if (std::fabs(dot) > 1.0e-5f * std::sqrt(lengthSq[a] * lengthSq[b])) {
                return true;
            }
        }
    }
    return false;
}

void Blend(const nw4r::math::MTX34& from, nw4r::math::MTX34& to, f32 t) {
    if (t <= 0.0f) {
        to = from;
        return;
    }
    if (t >= 1.0f) {
        return;
    }
    if (NeedsLinearBlend(from) || NeedsLinearBlend(to)) {
        BlendLinear(from, to, t);
        return;
    }
    const Pose a = Decompose(from);
    Pose b = Decompose(to);
    for (int i = 0; i < 3; i++) {
        b.t[i] = a.t[i] + (b.t[i] - a.t[i]) * t;
        b.s[i] = a.s[i] + (b.s[i] - a.s[i]) * t;
    }
    b.q = Nlerp(a.q, b.q, t);
    Compose(b, to);
}

// Element by element: exact at both ends, shear included (a pane's global matrix often has some: a
// rotating child under a parent with uneven scale, e.g. the clock's hands), and close enough in between
// for the small change of one tick
void BlendLinear(const nw4r::math::MTX34& from, nw4r::math::MTX34& to, f32 t) {
    if (t <= 0.0f) {
        to = from;
        return;
    }
    if (t >= 1.0f) {
        return;
    }
    for (int i = 0; i < 12; i++) {
        to.a[i] = from.a[i] + (to.a[i] - from.a[i]) * t;
    }
}

f32 DistanceSq(const nw4r::math::MTX34& a, const nw4r::math::MTX34& b) {
    f32 sum = 0.0f;
    for (int row = 0; row < 3; row++) {
        const f32 d = a.m[row][3] - b.m[row][3];
        sum += d * d;
    }
    return sum;
}

struct Record {
    const void* resMdl; // the same model (an object at a freed one's address is another)
    nw4r::math::MTX34 world;
    std::vector<nw4r::math::MTX34> nodes;
};

std::unordered_map<const nw4r::g3d::ScnObj*, Record> sRecords;

// The newest matrices of the models a frame between ticks blended, for RestoreWorld
struct Saved {
    nw4r::g3d::ScnMdlSimple* mdl;
    nw4r::math::MTX34 world;
    std::vector<nw4r::math::MTX34> nodes;
};
std::vector<Saved> sSaved;
size_t sSavedCount = 0;

template <typename F>
void ForEachModel(F&& f) {
    nw4r::g3d::ScnRoot* root = m3d::getScnRoot();
    if (root == nullptr) {
        return;
    }
    for (nw4r::g3d::ScnObj** it = root->Begin(); it != root->End(); ++it) {
        if (nw4r::g3d::ScnMdlSimple* mdl = nw4r::g3d::G3dObj::DynamicCast<nw4r::g3d::ScnMdlSimple>(*it)) {
            f(mdl);
        }
    }
}

// Layouts: each pane's global matrix and each layout's view matrix (its position, Simple_c::mPos)
std::unordered_map<const void*, nw4r::math::MTX34> sLayoutRecords;

bool IsEffectivelyVisible(const nw4r::lyt::Pane* pane) {
    for (; pane != nullptr; pane = pane->GetParent()) {
        if (!pane->IsVisible()) {
            return false;
        }
    }
    return true;
}

struct SavedMtx {
    nw4r::math::MTX34* target; // a layout's view matrix, or nullptr for a pane's
    nw4r::lyt::Pane* pane;
    nw4r::math::MTX34 mtx;
};
std::vector<SavedMtx> sLayoutSaved;

template <typename F>
void ForEachPane(nw4r::lyt::Pane* pane, F&& f) {
    f(pane);
    auto& children = pane->GetChildList(); // PaneList is protected
    for (auto it = children.GetBeginIter(); it != children.GetEndIter(); ++it) {
        ForEachPane(&*it, f);
    }
}

template <typename F>
void ForEachLayout(F&& f) {
    m2d::forEachDrawEntry(
        [](m2d::Base_c* entry, void* arg) {
            if (m2d::Simple_c* simple = dynamic_cast<m2d::Simple_c*>(entry)) {
                if (simple->mLayout.GetRootPane() != nullptr) {
                    (*static_cast<F*>(arg))(simple);
                }
            }
        },
        &f);
}

// The view matrix's storage in a DrawInfo (only the getter is const)
nw4r::math::MTX34* ViewMtx(m2d::Simple_c* simple) {
    return const_cast<nw4r::math::MTX34*>(&simple->mDrawInfo.GetViewMtx());
}

void BlendRecorded(const void* key, nw4r::math::MTX34& mtx, f32 t) {
    const auto it = sLayoutRecords.find(key);
    if (it != sLayoutRecords.end() && DistanceSq(it->second, mtx) <= kLayoutSnapDistance * kLayoutSnapDistance) {
        BlendLinear(it->second, mtx, t);
    }
}

} // namespace

void InvalidateLayoutHistory(nw4r::lyt::Pane* rootPane) {
    if (rootPane != nullptr) {
        ForEachPane(rootPane, [](nw4r::lyt::Pane* pane) { sLayoutRecords.erase(pane); });
    }
}

void RecordLayouts() {
    sLayoutRecords.clear();
    ForEachLayout([](m2d::Simple_c* simple) {
        if (IsEffectivelyVisible(simple->mLayout.GetRootPane())) {
            sLayoutRecords[simple] = simple->mDrawInfo.GetViewMtx();
        }
        ForEachPane(simple->mLayout.GetRootPane(),
                    [](nw4r::lyt::Pane* pane) {
                        // Hidden subtrees can retain matrices from much older ticks.
                        if (IsEffectivelyVisible(pane)) {
                            sLayoutRecords[pane] = pane->GetGlobalMtx();
                        }
                    });
    });
}

void InterpolateLayouts(f32 tickFraction) {
    sLayoutSaved.clear();
    ForEachLayout([tickFraction](m2d::Simple_c* simple) {
        nw4r::math::MTX34* view = ViewMtx(simple);
        sLayoutSaved.push_back({view, nullptr, *view});
        if (tickFraction < 1.0f && IsEffectivelyVisible(simple->mLayout.GetRootPane()) &&
            sLayoutRecords.find(simple->mLayout.GetRootPane()) != sLayoutRecords.end()) {
            BlendRecorded(simple, *view, tickFraction);
        }
        ForEachPane(simple->mLayout.GetRootPane(), [tickFraction](nw4r::lyt::Pane* pane) {
            nw4r::math::MTX34 mtx = pane->GetGlobalMtx();
            sLayoutSaved.push_back({nullptr, pane, mtx});
            if (tickFraction < 1.0f && IsEffectivelyVisible(pane)) {
                BlendRecorded(pane, mtx, tickFraction);
                pane->SetGlobalMtx(mtx);
            }
        });
    });
}

void RestoreLayouts() {
    for (const SavedMtx& saved : sLayoutSaved) {
        if (saved.target != nullptr) {
            *saved.target = saved.mtx;
        } else {
            saved.pane->SetGlobalMtx(saved.mtx);
        }
    }
    sLayoutSaved.clear();
}

void RecordWorld() {
    sRecords.clear();
    ForEachModel([](nw4r::g3d::ScnMdlSimple* mdl) {
        Record& record = sRecords[mdl];
        record.resMdl = mdl->GetResMdl().ptr();
        mdl->GetMtx(nw4r::g3d::ScnObj::MTX_WORLD, &record.world);
        const nw4r::math::MTX34* nodes = mdl->GetWldMtxArray();
        record.nodes.assign(nodes, nodes + mdl->GetNumViewMtx());
    });
}

void InterpolateWorld(f32 tickFraction) {
    sSavedCount = 0;
    if (tickFraction >= 1.0f) {
        return;
    }
    ForEachModel([tickFraction](nw4r::g3d::ScnMdlSimple* mdl) {
        const auto it = sRecords.find(mdl);
        if (it == sRecords.end()) {
            return; // new since the last tick: as it is
        }
        const Record& record = it->second;
        if (record.resMdl != mdl->GetResMdl().ptr() || record.nodes.size() != mdl->GetNumViewMtx()) {
            return;
        }
        nw4r::math::MTX34 world;
        mdl->GetMtx(nw4r::g3d::ScnObj::MTX_WORLD, &world);
        if (DistanceSq(record.world, world) > kSnapDistance * kSnapDistance) {
            return;
        }
        nw4r::math::MTX34* nodes = mdl->GetWldMtxArray();
        if (sSavedCount == sSaved.size()) {
            sSaved.emplace_back();
        }
        Saved& saved = sSaved[sSavedCount++];
        saved.mdl = mdl;
        saved.world = world;
        saved.nodes.assign(nodes, nodes + record.nodes.size());

        Blend(record.world, world, tickFraction);
        mdl->SetMtx(nw4r::g3d::ScnObj::MTX_WORLD, &world);
        for (size_t i = 0; i < record.nodes.size(); i++) {
            Blend(record.nodes[i], nodes[i], tickFraction);
        }
    });
}

void RestoreWorld() {
    for (size_t i = 0; i < sSavedCount; i++) {
        Saved& saved = sSaved[i];
        saved.mdl->SetMtx(nw4r::g3d::ScnObj::MTX_WORLD, &saved.world);
        nw4r::math::MTX34* nodes = saved.mdl->GetWldMtxArray();
        for (size_t n = 0; n < saved.nodes.size(); n++) {
            nodes[n] = saved.nodes[n];
        }
    }
    sSavedCount = 0;
}

} // namespace interp
} // namespace cf
