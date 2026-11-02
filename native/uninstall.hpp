#pragma once
#include "windows.hpp"
namespace hr {
struct InstalledApp {
    std::string name,publisher,version,command,location;
    std::wstring key;
    bool machine=false,msi=false;
    REGSAM view=0;
};
std::vector<InstalledApp> installed_apps();
void uninstall_app(HWND owner,const InstalledApp& app);
}
