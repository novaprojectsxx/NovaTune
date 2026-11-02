#pragma once
#include "core.hpp"
#include <windows.h>
#include <filesystem>
#include <map>
#include <mutex>
namespace hr {
std::wstring wide(const std::string& text);
std::string utf8(const std::wstring& text);
std::string error(DWORD code = GetLastError());
void check(DWORD result, const char* action);
std::string now();
std::filesystem::path migrate_app_folder(const std::filesystem::path& parent);
std::filesystem::path data_dir();
std::string read_shared_file(const std::filesystem::path& path,size_t limit=64*1024*1024);
void atomic_write(const std::filesystem::path& path, const std::string& text);
class Store {
public:
    Records rows;
    std::filesystem::path path;
    explicit Store(std::filesystem::path file = {});
    void save();
    std::string get(const std::string& key, const std::string& fallback = "") const;
    void set(const std::string& key, const std::string& value);
    Records of(const std::string& kind) const;
    void log(const std::string& action, const std::string& detail);
};
struct Process { DWORD pid; std::wstring name; double ram, cpu; unsigned long long created; };
struct Sample { double cpu = 0, used = 0, total = 0; std::vector<Process> processes; std::wstring gpu; double gpu_util=-1,vram_used=-1,vram_total=-1,disk_read=-1,disk_write=-1,gpu_temp=-1; };
Sample sample();
Records pc_info();
void close_app(const Process& process);
struct Plan { std::string guid; std::wstring name; };
std::vector<Plan> plans();
std::string active_plan();
void set_plan(const std::string& guid);
struct Startup { std::string name, scope, bytes; DWORD type; };
std::vector<Startup> startup();
void disable_startup(const Startup& entry);
void restore_startup(const Startup& entry);
struct Service { std::string name, display, path; DWORD start, state; bool delayed, eligible; };
std::vector<Service> services();
Service service(const std::string& name);
void set_service(const Service& original, DWORD mode, bool delayed, DWORD expected, bool expected_delayed);
std::string service_mode(DWORD mode, bool delayed = false);
struct Drivers { Records installed; std::vector<std::string> offers; std::string update_error; };
Drivers drivers(bool updates);
std::string vendor(const std::string& text);
void launch(const std::wstring& exe);
void shell(const std::wstring& target);
std::wstring pick(HWND owner, const wchar_t* filter);
std::wstring quote(const std::wstring& value);
DWORD run(const std::wstring& exe, const std::vector<std::wstring>& args, DWORD timeout_ms);
std::string capture(Store& store, const std::string& game, unsigned seconds);
void undo(Store& store);
void undo_at(Store& store,size_t index);
void switch_mode(Store& store, const std::string& mode);
void change_service(Store& store, const Service& entry, DWORD target);
void change_startup(Store& store, const Startup& entry);
void restore_saved_startup(Store& store, const Row& row);
}
