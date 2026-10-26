#pragma once

namespace cf {
namespace debug {

void draw();

// Before each game tick: what the debug UI asked of the game (the Scene menu's scene change)
void applyPending();
}
}
