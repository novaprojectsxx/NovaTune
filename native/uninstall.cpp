#include "uninstall.hpp"
#include <shellapi.h>
#include <objbase.h>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace hr {
namespace {
constexpr wchar_t uninstall_root[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
struct Key {HKEY h=nullptr;~Key(){if(h)RegCloseKey(h);}};
std::wstring string(HKEY key,const wchar_t* name){
    DWORD type=0,bytes=0;if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&bytes)!=ERROR_SUCCESS || (type!=REG_SZ && type!=REG_EXPAND_SZ) || bytes>131072)return {};
    std::vector<wchar_t> value(bytes/2+1,0);
    if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&bytes)!=ERROR_SUCCESS)return {};
    std::wstring result(value.data());
    if(type==REG_EXPAND_SZ){DWORD n=ExpandEnvironmentStringsW(result.c_str(),nullptr,0);std::wstring expanded(n,L'\0');if(n && ExpandEnvironmentStringsW(result.c_str(),&expanded[0],n)){expanded.resize(n-1);result=expanded;}}
    return result;
}
DWORD number(HKEY key,const wchar_t* name){
    DWORD type=0,value=0,n=sizeof value;if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&n)!=ERROR_SUCCESS || type!=REG_DWORD)return 0;return value;
}
InstalledApp read(HKEY key,const std::wstring& path,bool machine,REGSAM view){
    return {utf8(string(key,L"DisplayName")),utf8(string(key,L"Publisher")),utf8(string(key,L"DisplayVersion")),utf8(string(key,L"UninstallString")),utf8(string(key,L"InstallLocation")),path,machine,number(key,L"WindowsInstaller")==1,view};
}
}
std::vector<InstalledApp> installed_apps(){
    std::vector<InstalledApp> result;std::set<std::string> seen;
    for(bool machine:{false,true})for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){
        Key root;
        if(RegOpenKeyExW(machine?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER,uninstall_root,0,KEY_ENUMERATE_SUB_KEYS|view,&root.h)!=ERROR_SUCCESS)continue;
        for(DWORD index=0;;++index){
            wchar_t name[512]{};DWORD n=512;auto status=RegEnumKeyExW(root.h,index,name,&n,nullptr,nullptr,nullptr,nullptr);
            if(status==ERROR_NO_MORE_ITEMS)break;if(status!=ERROR_SUCCESS)continue;
            auto path=std::wstring(uninstall_root)+L"\\"+name;Key key;
            if(RegOpenKeyExW(machine?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER,path.c_str(),0,KEY_QUERY_VALUE|view,&key.h)!=ERROR_SUCCESS)continue;
            if(number(key.h,L"SystemComponent") || !string(key.h,L"ParentKeyName").empty())continue;
            auto app=read(key.h,path,machine,view);if(app.name.empty())continue;
            auto identity=lower(app.name+"|"+app.version+"|"+app.command);
            if(seen.insert(identity).second)result.push_back(std::move(app));
        }
    }
    std::sort(result.begin(),result.end(),[](const InstalledApp& a,const InstalledApp& b){return lower(a.name)<lower(b.name);});return result;
}
void uninstall_app(HWND owner,const InstalledApp& app){
    Key key;
    check(RegOpenKeyExW(app.machine?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER,app.key.c_str(),0,KEY_QUERY_VALUE|app.view,&key.h),"Recheck installed application");
    auto current=read(key.h,app.key,app.machine,app.view);
    if(number(key.h,L"SystemComponent") || current.command!=app.command || current.name!=app.name)throw std::runtime_error("Application changed or is protected. Refresh before trying again.");
    std::wstring executable,arguments;
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
    auto leaf=app.key.substr(app.key.find_last_of(L'\\')+1);
    GUID product{};
    if(app.msi && SUCCEEDED(CLSIDFromString(leaf.c_str(),&product))){
        executable=(std::filesystem::path(system)/L"msiexec.exe").wstring();arguments=L"/x "+leaf;
    }else{
        auto command=wide(app.command);
        if(command.empty())throw std::runtime_error("This app has no registered uninstaller. Use Windows Apps settings.");
        if(command.front()==L'"'){
            auto end=command.find(L'"',1);if(end==std::wstring::npos)throw std::runtime_error("Invalid uninstaller command");
            executable=command.substr(1,end-1);arguments=command.substr(end+1);
        }else{
            auto lower_command=lower(utf8(command));auto end=lower_command.find(".exe");
            if(end==std::string::npos)throw std::runtime_error("Unsupported uninstaller command. Use Windows Apps settings.");
                                                                      
            auto wide_lower=command;std::transform(wide_lower.begin(),wide_lower.end(),wide_lower.begin(),towlower);
            auto stop=wide_lower.find(L".exe")+4;executable=command.substr(0,stop);arguments=command.substr(stop);
        }
        if(!std::filesystem::path(executable).is_absolute()){
            auto name=lower(utf8(executable));
            if(name!="msiexec.exe" && name!="rundll32.exe" && name!="cmd.exe")throw std::runtime_error("The registered uninstaller needs Windows Apps settings.");
            executable=(std::filesystem::path(system)/executable).wstring();
        }
        if(!std::filesystem::is_regular_file(std::filesystem::path(executable)))throw std::runtime_error("The registered uninstaller no longer exists.");
    }
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof info;info.hwnd=owner;info.lpVerb=L"open";info.lpFile=executable.c_str();info.lpParameters=arguments.c_str();info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info))throw std::runtime_error(error());
}
}
