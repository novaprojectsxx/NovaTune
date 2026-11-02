#pragma once
#include "devices.hpp"
namespace hr {
struct GameProfile {std::string game,preset="everyday",power,backup_folder,device_profile;bool animations=false,enabled=false;unsigned retention=5;};
GameProfile game_profile(const Store& store,const std::string& game);
void save_game_profile(Store& store,const GameProfile& profile);
SessionSettings profile_settings(const Store& store,const GameProfile& profile);
std::string profile_review(const GameProfile& profile);
}
