// Interpolated models for frames between ticks (src/cf/game/interp.cpp, cf/frame.h).
//
// Each tick's frame records the world matrices of the models it draws (after m3d::calcWorld); a frame
// between ticks blends each model's matrices (the model's and its nodes') from those to the ones the
// newest logic gives, so movement and animation are smooth. A model that jumps further than a
// threshold in a tick (a warp, a respawn) is not blended.
#pragma once

#include <types.h>

namespace nw4r { namespace lyt { class Pane; } }
namespace m2d { class Simple_c; }

namespace cf {
namespace interp {

// A tick's frame, after m3d::calcWorld: the drawn models' world matrices
void RecordWorld();

// A frame between ticks, after m3d::calcWorld: the drawn models' world matrices blended from the
// recorded ones by tickFraction (0: the recorded, 1: the newest)
void InterpolateWorld(f32 tickFraction);

// After that frame is drawn: the newest matrices back. Not every model's are recomputed by the next
// m3d::calcWorld (some are calculated only by their own code, in the logic, e.g. the player's), and
// the blend must start from the newest, not from the last frame's blend.
void RestoreWorld();

// The same for the 2D layouts in the draw list: each pane's global matrix and each layout's view matrix
// (its position). A tick's frame records them; a frame between ticks blends them before drawing and
// restores them after.
void RecordLayouts();
// Call when panes are rebound to different content or their animation is reset.
// Discards the previous tick's positions for this entire subtree (nullptr is safe).
void InvalidateLayoutHistory(nw4r::lyt::Pane* rootPane);
void InterpolateLayouts(f32 tickFraction);
void RestoreLayouts();
// A layout drawn where its tick put it, never blended (the pointer cursor, d_obj_cursor: it follows the
// pointer as the game read it). Until it is allowed again.
void SetLayoutInterpolated(const m2d::Simple_c* layout, bool interpolated);

} // namespace interp
} // namespace cf
