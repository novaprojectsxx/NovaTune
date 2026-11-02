#pragma once
#include "windows.hpp"
namespace hr {
void release_standby();
std::filesystem::path maintenance_exe();
void memory_service(Store& store,bool enable);
int run_memory_service();
void undo_memory_service(const Row& row);
}
