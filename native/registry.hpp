#pragma once
#include "windows.hpp"
namespace hr {
void registry_backup(Store& store,HKEY hive,const std::wstring& path);
std::string registry_value(HKEY hive,const std::wstring& path,const std::wstring& name);
void registry_change(Store& store,HKEY hive,const std::wstring& path,const std::wstring& name,DWORD type,const std::string& bytes,const std::string& label);
void registry_dword(Store& store,HKEY hive,const std::wstring& path,const std::wstring& name,DWORD value,const std::string& label);
void undo_registry_value(const Row& row);
std::filesystem::path system_exe(const wchar_t* name);
std::string read_output(const std::filesystem::path& path);
struct CommandResult {DWORD code;std::string output;std::filesystem::path log;};
CommandResult command_output(const std::wstring& executable,const std::vector<std::wstring>& args,const std::filesystem::path& log={},DWORD timeout=3600000);
}
