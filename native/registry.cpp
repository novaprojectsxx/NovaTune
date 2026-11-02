#include "registry.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>
#include <stdexcept>
namespace hr {
namespace {
struct Handle {HANDLE h;explicit Handle(HANDLE value):h(value){}~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
std::wstring hive_name(HKEY hive){if(hive==HKEY_CURRENT_USER)return L"HKCU";if(hive==HKEY_LOCAL_MACHINE)return L"HKLM";throw std::runtime_error("Unsupported registry hive");}
bool allowed(const std::wstring& path,const std::wstring& name){
    const std::set<std::wstring> roots={
        L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsAI",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Paint",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\WindowsAI",
        L"Software\\Policies\\Microsoft\\Windows\\WindowsCopilot",
        L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
        L"Software\\Microsoft\\GameBar",
        L"System\\GameConfigStore",
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
        L"SOFTWARE\\Microsoft\\Windows\\Dwm",
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Search",
        L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search",
        L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked"
    };
    if(roots.count(path))return true;
    return path==L"Software\\Headroom\\Test" && name==L"Fixture";
}
void write(HKEY hive,const std::wstring& path,const std::wstring& name,const std::string& state){
    if(!allowed(path,name))throw std::runtime_error("Registry target is not in NovaTune's audited settings");
    HKEY key=nullptr;check(RegCreateKeyExW(hive,path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr),"Open registry setting");
    LONG result=ERROR_SUCCESS;
    if(state=="-"||state=="k"){
        result=RegDeleteValueW(key,name.c_str());if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;
    }else{
        auto colon=state.find(':');if(colon==std::string::npos){RegCloseKey(key);throw std::runtime_error("Invalid registry baseline");}
        auto type=static_cast<DWORD>(std::stoul(state.substr(0,colon)));auto bytes=unhex(state.substr(colon+1));
        result=RegSetValueExW(key,name.c_str(),0,type,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()));
    }
    RegCloseKey(key);check(result,"Apply registry setting");
    if(state=="-"){
        HKEY inspect=nullptr;
        if(RegOpenKeyExW(hive,path.c_str(),0,KEY_QUERY_VALUE|KEY_ENUMERATE_SUB_KEYS,&inspect)==ERROR_SUCCESS){
            DWORD subkeys=0,values=0;RegQueryInfoKeyW(inspect,nullptr,nullptr,nullptr,&subkeys,nullptr,nullptr,&values,nullptr,nullptr,nullptr,nullptr);RegCloseKey(inspect);
            if(!subkeys && !values)RegDeleteKeyW(hive,path.c_str());
        }
    }
}
}
std::filesystem::path system_exe(const wchar_t* name){wchar_t root[MAX_PATH]{};GetSystemDirectoryW(root,MAX_PATH);return std::filesystem::path(root)/name;}
std::string read_output(const std::filesystem::path& path){
                                                                          
    std::string bytes=read_shared_file(path);
    bool unicode=bytes.compare(0,2,"\xff\xfe")==0;
    if(!unicode && bytes.size()>20){size_t zeros=0;for(size_t i=1;i<std::min<size_t>(bytes.size(),1000);i+=2)zeros+=bytes[i]==0;unicode=zeros>std::min<size_t>(bytes.size(),1000)/8;}
    if(unicode){
        size_t start=bytes.compare(0,2,"\xff\xfe")==0?2:0;std::wstring text((bytes.size()-start)/2,L'\0');
        if(!text.empty())memcpy(&text[0],bytes.data()+start,text.size()*2);return utf8(text);
    }
    if(bytes.compare(0,3,"\xef\xbb\xbf")==0)bytes.erase(0,3);
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0)>0)return bytes;
    int n=MultiByteToWideChar(CP_OEMCP,0,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    std::wstring text(n,L'\0');if(n)MultiByteToWideChar(CP_OEMCP,0,bytes.data(),static_cast<int>(bytes.size()),&text[0],n);return utf8(text);
}
CommandResult command_output(const std::wstring& exe,const std::vector<std::wstring>& args,const std::filesystem::path& requested,DWORD timeout){
    auto log=requested.empty()?data_dir()/L"logs"/(L"command-"+std::to_wstring(GetTickCount64())+L".log"):requested;
    std::filesystem::create_directories(log.parent_path());SECURITY_ATTRIBUTES sa{sizeof sa,nullptr,TRUE};
    Handle output(CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    Handle null(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
    if(output.h==INVALID_HANDLE_VALUE||null.h==INVALID_HANDLE_VALUE)throw std::runtime_error(error());
    std::wstring command=quote(exe);for(const auto& arg:args)command+=L" "+quote(arg);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput=output.h;startup.StartupInfo.hStdError=output.h;startup.StartupInfo.hStdInput=null.h;
    SIZE_T n=0;InitializeProcThreadAttributeList(nullptr,1,0,&n);std::vector<BYTE> memory(n);
    startup.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&n))throw std::runtime_error(error());
    HANDLE handles[]={output.h,null.h};
    auto updated=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof handles,nullptr,nullptr);
    PROCESS_INFORMATION raw{};BOOL created=updated && CreateProcessW(exe.c_str(),&command[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&raw);
    auto launch_error=GetLastError();DeleteProcThreadAttributeList(startup.lpAttributeList);
    if(!created)throw std::runtime_error(error(launch_error));
    Handle process(raw.hProcess),thread(raw.hThread);
    if(WaitForSingleObject(process.h,timeout)!=WAIT_OBJECT_0)throw std::runtime_error("Operation is still running or timed out. It was not force-terminated; inspect its log.");
    DWORD code=0;GetExitCodeProcess(process.h,&code);FlushFileBuffers(output.h);
    return {code,read_output(log),log};
}
void registry_backup(Store& store,HKEY hive,const std::wstring& path){
    static std::atomic<unsigned> sequence{0};
    auto root=store.path.parent_path()/L"registry-backups"/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence));std::filesystem::create_directories(root);
    auto hive_text=hive_name(hive),target=hive_text+L"\\"+path;
    auto file=root/L"baseline.reg";HKEY key=nullptr;auto status=RegOpenKeyExW(hive,path.c_str(),0,KEY_READ,&key);
    if(status==ERROR_FILE_NOT_FOUND){file=root/L"absent.txt";atomic_write(file,"Registry key did not exist: "+utf8(target));}
    else{
        check(status,"Open registry path for backup");RegCloseKey(key);
        auto result=command_output(system_exe(L"reg.exe").wstring(),{L"export",target,file.wstring(),L"/y"},root/L"export.log",30000);
        if(result.code)throw std::runtime_error("Registry backup failed; no tweak applied. "+result.output);
    }
    store.rows.push_back({"backup",now(),utf8(hive_text),utf8(path),utf8(file.wstring())});store.save();
}
std::string registry_value(HKEY hive,const std::wstring& path,const std::wstring& name){
    HKEY key=nullptr;auto status=RegOpenKeyExW(hive,path.c_str(),0,KEY_QUERY_VALUE,&key);
    if(status==ERROR_FILE_NOT_FOUND)return "-";check(status,"Read registry baseline");
    DWORD type=0,n=0;status=RegQueryValueExW(key,name.c_str(),nullptr,&type,nullptr,&n);
    if(status==ERROR_FILE_NOT_FOUND){RegCloseKey(key);return "k";}
    if(status){RegCloseKey(key);check(status,"Read registry baseline");}
    std::string bytes(n,'\0');status=RegQueryValueExW(key,name.c_str(),nullptr,&type,n?reinterpret_cast<BYTE*>(&bytes[0]):nullptr,&n);RegCloseKey(key);
    check(status,"Read registry baseline");return std::to_string(type)+":"+hex(bytes);
}
void registry_change(Store& store,HKEY hive,const std::wstring& path,const std::wstring& name,DWORD type,const std::string& bytes,const std::string& label){
    if(!allowed(path,name))throw std::runtime_error("Unknown registry optimization");
    auto before=registry_value(hive,path,name),after=std::to_string(type)+":"+hex(bytes);if(before==after)return;
    registry_backup(store,hive,path);
    store.rows.push_back({"journal","regvalue",utf8(hive_name(hive))+":"+utf8(path),before,after,utf8(name)});store.save();
    write(hive,path,name,after);store.log("Setting changed",label);
}
void registry_dword(Store& store,HKEY hive,const std::wstring& path,const std::wstring& name,DWORD value,const std::string& label){
    registry_change(store,hive,path,name,REG_DWORD,std::string(reinterpret_cast<const char*>(&value),sizeof value),label);
}
void undo_registry_value(const Row& row){
    auto colon=row[2].find(':');auto hive=row[2].substr(0,colon)=="HKCU"?HKEY_CURRENT_USER:HKEY_LOCAL_MACHINE;
    auto path=wide(row[2].substr(colon+1)),name=wide(row[5]);
    if(!allowed(path,name))throw std::runtime_error("Unknown registry recovery target");
    auto current=registry_value(hive,path,name);if(current==row[3])return;
    if(current!=row[4])throw std::runtime_error("Setting changed outside NovaTune; baseline was not overwritten");
    write(hive,path,name,row[3]);
}
}
