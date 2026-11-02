#pragma once
#include "windows.hpp"
#include <set>
namespace hr {
struct ControllerHotspot {RECT rect;unsigned code;};
std::vector<ControllerHotspot> draw_controller_art(HDC dc,RECT area,bool playstation,const std::set<unsigned>& pressed,COLORREF highlight);
}
