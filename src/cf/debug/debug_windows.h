// The debug UI's windows (src/cf/debug), drawn by debug_ui.cpp while open
#pragma once

#include <string>

namespace cf {
namespace debug {

// open: the window's close button clears it
void DrawInputWindow(bool* open); // debug_input.cpp

// Writes this frame's 2D draw list to layout_dump.txt in the user directory (its path in *path)
bool DumpLayouts(std::string* path); // debug_layouts.cpp

// The main menu bar's Scene menu
void DrawSceneMenu(); // debug_scene.cpp

} // namespace debug
} // namespace cf
