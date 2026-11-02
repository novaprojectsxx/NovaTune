#pragma once
#include "windows.hpp"
namespace hr {
struct DisplayMode {std::string device,label;int width=0,height=0,hz=0,bits=32,x=0,y=0,orientation=0;DWORD flags=0;};
std::vector<DisplayMode> current_displays();
std::vector<DisplayMode> supported_display_modes(const std::string& device);
std::string encode_displays(const std::vector<DisplayMode>& modes);
std::vector<DisplayMode> decode_displays(const std::string& text);
bool same_displays(const std::string& a,const std::string& b);
void apply_displays(const std::string& desired);
struct DeviceProfile {std::string name,displays,playback,microphone;};
Records device_profiles(const Store& store);
DeviceProfile read_device_profile(const Row& row);
void save_device_profile(Store& store,const DeviceProfile& profile);
struct SessionSettings {std::string power,displays,playback,microphone;bool reduce_animations=false;};
class SettingsGuard {
    HANDLE stop_event=nullptr,commit_event=nullptr,child=nullptr;
    std::filesystem::path record_file;
public:
    SettingsGuard(const Process& target,const SessionSettings& settings,unsigned rollback_seconds=0);
    ~SettingsGuard();
    void keep();
    const std::filesystem::path& recovery_file()const{return record_file;}
    SettingsGuard(const SettingsGuard&)=delete;
};
int settings_worker(int argc,wchar_t** argv);
std::string settings_recovery(bool restore=false);
std::string undo_device_preview(const std::filesystem::path& record);
}
