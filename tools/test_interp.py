"""Compile and exercise the production interpolation routines without game assets.

Uses the math and layout sections verbatim from interp.cpp, with minimal layout
stand-ins so visibility, history invalidation, and restoration can be tested.
Run with Python in a C++ compiler environment (cl on Windows, c++ elsewhere).
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#include <cassert>
#include <cmath>
#include <list>
#include <unordered_map>
#include <vector>
#include <iostream>
using f32 = float;
namespace nw4r {
namespace math {
struct MTX34 {
    union { float m[3][4]; float a[12]; };
    MTX34() : m{{1,0,0,0},{0,1,0,0},{0,0,1,0}} {}
};
}
namespace lyt {
struct Pane;
struct Children {
    std::list<Pane> panes;
    auto GetBeginIter() { return panes.begin(); }
    auto GetEndIter() { return panes.end(); }
};
struct Pane {
    Pane* parent = nullptr;
    bool visible = true;
    math::MTX34 matrix;
    Children children;
    bool IsVisible() const { return visible; }
    Pane* GetParent() const { return parent; }
    Children& GetChildList() { return children; }
    const math::MTX34& GetGlobalMtx() const { return matrix; }
    void SetGlobalMtx(const math::MTX34& m) { matrix = m; }
    Pane& AddChild() {
        children.panes.emplace_back();
        auto& child = children.panes.back();
        child.parent = this;
        return child;
    }
};
}
}
namespace m2d {
struct Base_c { virtual ~Base_c() = default; };
struct Simple_c : Base_c {
    struct Layout {
        nw4r::lyt::Pane root;
        nw4r::lyt::Pane* GetRootPane() { return &root; }
    } mLayout;
    struct DrawInfo {
        nw4r::math::MTX34 matrix;
        const nw4r::math::MTX34& GetViewMtx() const { return matrix; }
    } mDrawInfo;
};
std::vector<Base_c*> entries;
void forEachDrawEntry(void (*f)(Base_c*, void*), void* arg) {
    for (auto* entry : entries) f(entry, arg);
}
}
'''

TESTS = r'''
} // interp
} // cf
using namespace cf::interp;
using nw4r::math::MTX34;
static void equal(const MTX34& a, const MTX34& b) {
    for (int i = 0; i < 12; ++i) assert(a.a[i] == b.a[i]);
}
int main() {
    MTX34 shear;
    shear.m[0][1] = 0.75f;
    assert(NeedsLinearBlend(shear));
    for (float t : {0.f, 0.25f, 0.5f, 0.99f, 1.f}) {
        MTX34 result = shear;
        Blend(shear, result, t);
        equal(result, shear); // No deformation even on an unchanged sheared model.
    }
    MTX34 identity, result;
    result = identity;
    Blend(shear, result, 0.5f);
    assert(result.m[0][1] == 0.375f);
    result = shear;
    Blend(identity, result, 0.5f);
    assert(result.m[0][1] == 0.375f); // Either endpoint may contain shear.
    result = shear;
    Blend(identity, result, 0.f);
    equal(result, identity);
    result = shear;
    Blend(identity, result, 1.f);
    equal(result, shear);
    MTX34 rotated;
    rotated.m[0][0] = 0; rotated.m[0][1] = -1;
    rotated.m[1][0] = 1; rotated.m[1][1] = 0;
    assert(!NeedsLinearBlend(rotated));
    result = rotated;
    Blend(identity, result, 0.5f);
    assert(std::fabs(result.m[0][0] - std::sqrt(0.5f)) < 1e-5f);
    assert(std::fabs(result.m[1][0] - std::sqrt(0.5f)) < 1e-5f);
    MTX34 scaled = rotated;
    for (int row = 0; row < 3; ++row) scaled.m[row][0] *= -2.f;
    assert(!NeedsLinearBlend(scaled)); // Mirroring/uneven scale alone is not shear.
    MTX34 collapsed;
    collapsed.m[0][0] = 0;
    assert(NeedsLinearBlend(collapsed));

    m2d::Simple_c layout;
    m2d::entries = {&layout};
    auto& root = layout.mLayout.root;
    auto& row = root.AddChild();
    auto& text = row.AddChild();
    auto& sibling = root.AddChild();
    RecordLayouts();
    row.matrix.m[1][3] = text.matrix.m[1][3] = sibling.matrix.m[1][3] = 40;
    InterpolateLayouts(0.5f);
    assert(text.matrix.m[1][3] == 20);
    RestoreLayouts();
    assert(text.matrix.m[1][3] == 40);
    InvalidateLayoutHistory(&row);
    InvalidateLayoutHistory(nullptr);
    for (float t : {0.25f, 0.75f}) {
        InterpolateLayouts(t);
        assert(row.matrix.m[1][3] == 40 && text.matrix.m[1][3] == 40);
        assert(sibling.matrix.m[1][3] == 40 * t);
        RestoreLayouts();
    }
    RecordLayouts(); // Invalidated panes acquire fresh history on the next tick.
    text.matrix.m[1][3] = 60;
    InterpolateLayouts(0.5f);
    assert(text.matrix.m[1][3] == 50);
    RestoreLayouts();

    row.visible = false; // Visible child beneath a hidden ancestor has stale matrices.
    RecordLayouts();
    row.visible = true;
    text.matrix.m[1][3] = 80;
    InterpolateLayouts(0.5f);
    assert(text.matrix.m[1][3] == 80);
    RestoreLayouts();
    text.visible = false;
    RecordLayouts();
    text.visible = true;
    text.matrix.m[1][3] = 100;
    InterpolateLayouts(0.5f);
    assert(text.matrix.m[1][3] == 100);
    RestoreLayouts();
    RecordLayouts();
    row.visible = false;
    text.matrix.m[1][3] = 110;
    InterpolateLayouts(0.5f);
    assert(text.matrix.m[1][3] == 110); // Hidden at the current endpoint.
    RestoreLayouts();
    row.visible = true;
    RecordLayouts();
    layout.mDrawInfo.matrix.m[0][3] = 40;
    InvalidateLayoutHistory(&root);
    InterpolateLayouts(0.5f);
    assert(layout.mDrawInfo.matrix.m[0][3] == 40); // No stale layout translation either.
    layout.mDrawInfo.matrix.m[0][3] = 99;
    RestoreLayouts();
    assert(layout.mDrawInfo.matrix.m[0][3] == 40);
    std::cout << "Interpolation regression checks passed\n";
}
'''


def main():
    source = (ROOT / 'src/cf/game/interp.cpp').read_text(encoding='utf-8')
    math = source[source.index('namespace cf {'):source.index('struct Record {')]
    layouts = source[source.index('// Layouts:'):source.index('void RecordWorld()')]
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cf-interp-', dir=build) as directory:
        directory = Path(directory)
        cpp = directory / 'interp_test.cpp'
        exe = directory / ('interp_test.exe' if os.name == 'nt' else 'interp_test')
        cpp.write_text(STUBS + math + layouts + TESTS, encoding='utf-8')
        if os.name == 'nt':
            command = ['cl', '/nologo', '/EHsc', '/std:c++17', str(cpp), '/Fe:' + str(exe)]
        else:
            command = ['c++', '-std=c++17', str(cpp), '-o', str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
