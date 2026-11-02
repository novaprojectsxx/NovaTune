#pragma once
#include "registry.hpp"
namespace hr {
struct SystemTool {std::string id,category,name,description;bool changes=false,restart=false,security=false;};
std::vector<SystemTool> system_tools();
std::string inspect_tool(const std::string& id);
CommandResult run_system_tool(Store& store,const std::string& id,const std::filesystem::path& log);
std::filesystem::path winget_path();
CommandResult winget(const std::string& action,const std::string& query,const std::filesystem::path& log);
void schedule_winget(Store& store,const std::string& ids,bool enable);
std::string powershell_text(const std::wstring& script,DWORD timeout=90000);
void undo_power_setting(const Row& row);
}
