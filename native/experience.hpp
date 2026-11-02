#pragma once
#include "desktop.hpp"
namespace hr {
bool section_matches(const std::string& label,const std::string& query);
std::string game_center_report(const Store& store,const std::string& game);
std::string change_explanation(const Row& journal);
DesktopScene portable_scene(DesktopScene scene);
std::string shared_scene_file(const DesktopScene& scene);
DesktopScene read_shared_scene(const std::string& text);
}
