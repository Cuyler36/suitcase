#pragma once

namespace cf {
namespace debug {

void draw();

// The user directory (aurora's), where the debug UI writes its dumps
void SetUserPath(const char* path);

// Before each game tick: what the debug UI asked of the game (the Scene menu's scene change)
void applyPending();
}
}
