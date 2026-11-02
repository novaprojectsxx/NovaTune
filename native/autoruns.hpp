#pragma once
#include "windows.hpp"
namespace hr {
struct Autorun {std::string kind,name,scope,command,target,extra,risk;bool enabled=true,editable=false;};
std::vector<Autorun> autoruns();
void disable_autorun(Store& store,const Autorun& entry);
void restore_autorun(Store& store,const Row& record);
void undo_autorun(const Row& journal);
Row autorun_record(const Autorun& entry);
Autorun autorun_from_record(const Row& row);
}
