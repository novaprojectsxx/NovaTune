#pragma once
#include "windows.hpp"
namespace hr {
std::string nvidia_cache_info();
void nvidia_cache_size(Store& store,unsigned megabytes);
void undo_nvidia_cache(const Row& row);
}
