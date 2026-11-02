#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <optional>
namespace hr {
struct FixItem {std::string id;std::wstring title,description;};
void app_fix_review(HWND owner,const std::vector<FixItem>& items,const std::function<void(HWND,const std::string&)>& apply,bool light=false);
void update_fix_review(HWND window,const std::string& id,const std::wstring& status,bool complete);
std::optional<RECT> app_region(HWND owner);
std::optional<RECT> app_crop_region(HWND owner,const std::wstring& image,bool light=false);
void app_toast(HWND owner,const std::wstring& message,bool light=false);
void app_mode_toast(HWND owner,const std::wstring& mode,const std::wstring& detail,const std::wstring& shortcut,bool light=false);
bool app_timed_confirm(HWND owner,const std::wstring& title,const std::wstring& message,unsigned seconds=15,bool light=false);
bool app_dialog(HWND owner,const std::wstring& title,const std::wstring& message,bool confirmation=false,bool light=false);
std::wstring app_folder(HWND owner,const std::wstring& title,bool light=false);
std::wstring app_file(HWND owner,const std::wstring& title,const std::wstring& extension,bool light=false);
}
