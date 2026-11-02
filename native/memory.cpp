#include "memory.hpp"
#include "registry.hpp"
#include <shlobj.h>
#include <fstream>
#include <stdexcept>
namespace hr {
namespace {
constexpr wchar_t service_name[]=L"HeadroomMemory";
SERVICE_STATUS_HANDLE status_handle=nullptr;
SERVICE_STATUS status{};
HANDLE stop_event=nullptr;
void report(DWORD state,DWORD error=NO_ERROR){
    status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;status.dwWin32ExitCode=error;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;status.dwWaitHint=state==SERVICE_STOP_PENDING?5000:0;
    SetServiceStatus(status_handle,&status);
}
void WINAPI control(DWORD code){if(code==SERVICE_CONTROL_STOP||code==SERVICE_CONTROL_SHUTDOWN){report(SERVICE_STOP_PENDING);SetEvent(stop_event);}}
void WINAPI entry(DWORD,wchar_t**){
    status_handle=RegisterServiceCtrlHandlerW(service_name,control);if(!status_handle)return;
    stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop_event){report(SERVICE_STOPPED,GetLastError());return;}
    report(SERVICE_RUNNING);ULONGLONG last=0;
    while(WaitForSingleObject(stop_event,60000)==WAIT_TIMEOUT){
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(!GlobalMemoryStatusEx(&memory))continue;
                                                                              
                                                                               
        if(memory.ullAvailPhys<512ULL*1024*1024 && GetTickCount64()-last>=300000){
            try{release_standby();last=GetTickCount64();}catch(const std::exception&){last=GetTickCount64();}
        }
    }
    CloseHandle(stop_event);stop_event=nullptr;report(SERVICE_STOPPED);
}
void remove_service(){
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)throw std::runtime_error(error());
    SC_HANDLE service=OpenServiceW(manager,service_name,SERVICE_QUERY_CONFIG|SERVICE_STOP|SERVICE_QUERY_STATUS|DELETE);
    if(!service){auto code=GetLastError();CloseServiceHandle(manager);if(code==ERROR_SERVICE_DOES_NOT_EXIST)return;throw std::runtime_error(error(code));}
    DWORD bytes=0;QueryServiceConfigW(service,nullptr,0,&bytes);std::vector<BYTE> memory(bytes);
    auto config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(memory.data());
    if(!QueryServiceConfigW(service,config,bytes,&bytes)||std::wstring(config->lpBinaryPathName).find(L"HeadroomMaintenance.exe")==std::wstring::npos){CloseServiceHandle(service);CloseServiceHandle(manager);throw std::runtime_error("Service identity differs; no unrelated service was changed");}
    SERVICE_STATUS current{};ControlService(service,SERVICE_CONTROL_STOP,&current);
    for(int n=0;n<50;++n){if(!QueryServiceStatus(service,&current)||current.dwCurrentState==SERVICE_STOPPED)break;Sleep(100);}
    auto ok=DeleteService(service);auto code=GetLastError();CloseServiceHandle(service);CloseServiceHandle(manager);if(!ok)throw std::runtime_error(error(code));
}
}
void release_standby(){
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))throw std::runtime_error(error());
    LUID privilege{};if(!LookupPrivilegeValueW(nullptr,L"SeProfileSingleProcessPrivilege",&privilege)){CloseHandle(token);throw std::runtime_error(error());}
    TOKEN_PRIVILEGES requested{};requested.PrivilegeCount=1;requested.Privileges[0].Luid=privilege;requested.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    TOKEN_PRIVILEGES previous{};DWORD bytes=sizeof previous;
    SetLastError(ERROR_SUCCESS);BOOL adjusted=AdjustTokenPrivileges(token,FALSE,&requested,sizeof previous,&previous,&bytes);DWORD result=GetLastError();
    if(!adjusted||result==ERROR_NOT_ALL_ASSIGNED){CloseHandle(token);throw std::runtime_error("Standby cleanup requires the profiling privilege; no memory was changed");}
    using SetInformation=LONG(NTAPI*)(ULONG,void*,ULONG);
    auto call=reinterpret_cast<SetInformation>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtSetSystemInformation"));
    ULONG command=4;LONG status=call?call(80,&command,sizeof command):static_cast<LONG>(0xc0000002);
    AdjustTokenPrivileges(token,FALSE,&previous,0,nullptr,nullptr);CloseHandle(token);
    if(status<0)throw std::runtime_error("This Windows build rejected standby cleanup (native status "+std::to_string(static_cast<unsigned long>(status))+")");
}
std::filesystem::path maintenance_exe(){
    PWSTR root=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&root)))throw std::runtime_error("Program Files is unavailable");
    auto directory=migrate_app_folder(std::filesystem::path(root));CoTaskMemFree(root);
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);auto target=directory/L"HeadroomMaintenance.exe";
    if(std::filesystem::path(self)!=target)std::filesystem::copy_file(self,target,std::filesystem::copy_options::overwrite_existing);
    return target;
}
void memory_service(Store& store,bool enable){
    if(!enable){remove_service();store.log("Standby agent disabled","Only the compatible standby service was removed");return;}
    auto executable=maintenance_exe();registry_backup(store,HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\HeadroomMemory");
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CREATE_SERVICE|SC_MANAGER_CONNECT);if(!manager)throw std::runtime_error(error());
    SC_HANDLE existing=OpenServiceW(manager,service_name,SERVICE_QUERY_STATUS);
    if(existing){CloseServiceHandle(existing);CloseServiceHandle(manager);throw std::runtime_error("NovaTune standby agent is already installed");}
    auto command=quote(executable.wstring())+L" --memory-service";
    store.rows.push_back({"journal","memory-service","","absent","installed",utf8(executable.wstring())});store.save();
    SC_HANDLE service=CreateServiceW(manager,service_name,L"NovaTune Standby Cache Agent",SERVICE_START|SERVICE_QUERY_STATUS,SERVICE_WIN32_OWN_PROCESS,SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,command.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr);
    if(!service){auto code=GetLastError();CloseServiceHandle(manager);throw std::runtime_error(error(code));}
    BOOL started=StartServiceW(service,0,nullptr);auto code=GetLastError();CloseServiceHandle(service);CloseServiceHandle(manager);if(!started)throw std::runtime_error(error(code));
    store.log("Standby agent enabled","Pressure-only cache release below 512 MB available; minimum five-minute interval");
}
int run_memory_service(){
    SERVICE_TABLE_ENTRYW table[]={{const_cast<wchar_t*>(service_name),entry},{nullptr,nullptr}};
    return StartServiceCtrlDispatcherW(table)?0:static_cast<int>(GetLastError());
}
void undo_memory_service(const Row&){remove_service();}
}
