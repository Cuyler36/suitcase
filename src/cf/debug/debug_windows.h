// The debug UI's windows (src/cf/debug), drawn by debug_ui.cpp while open
#pragma once

#include <string>

namespace cf {
namespace debug {

// open: the window's close button clears it
void DrawInputWindow(bool* open); // debug_input.cpp

// Writes this frame's 2D draw list to layout_dump.txt in the user directory (its path in *path)
bool DumpLayouts(std::string* path); // debug_layouts.cpp

// The user directory (SetUserPath, cf/debug.h)
const std::string& UserPath(); // debug_layouts.cpp

// The friend card window (debug_friend_card.cpp), opened from the Online menu
void DrawFriendCardWindow(bool* open);

// The main menu bar's Scene menu
void DrawSceneMenu(); // debug_scene.cpp

// The main menu bar's Online menu, and its windows (drawn outside the bar)
void DrawOnlineMenu();    // debug_online.cpp
void DrawOnlineWindows(); // debug_online.cpp

} // namespace debug
} // namespace cf
