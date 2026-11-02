#include "session.hpp"
#include <sddl.h>
#include <algorithm>
#include <stdexcept>
namespace hr {
namespace {
using Suspend=LONG(NTAPI*)(HANDLE);
struct Shared {volatile LONG status;};
unsigned long long ticks(FILETIME value){return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
bool system_path(std::wstring path){
    wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
    std::transform(path.begin(),path.end(),path.begin(),towlower);std::wstring root(windows);std::transform(root.begin(),root.end(),root.begin(),towlower);root+=L'\\';return path.compare(0,root.size(),root)==0;
}
std::vector<BYTE> user(HANDLE process){
    HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))throw std::runtime_error(error());
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> buffer(size);
    auto ok=GetTokenInformation(token,TokenUser,buffer.data(),size,&size);CloseHandle(token);if(!ok)throw std::runtime_error(error());return buffer;
}
}
bool sleep_eligible(const Process& entry,std::string& reason){
    if(entry.pid<=4||entry.pid==GetCurrentProcessId()){reason="Windows and NovaTune processes are protected";return false;}
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.pid);if(!process){reason=error();return false;}
    struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{process};
    FILETIME created{},exit{},kernel{},cpu{};
    if(!GetProcessTimes(process,&created,&exit,&kernel,&cpu)||ticks(created)!=entry.created){reason="Process changed; refresh first";return false;}
    DWORD session=0,self=0;ProcessIdToSessionId(entry.pid,&session);ProcessIdToSessionId(GetCurrentProcessId(),&self);
    if(session!=self){reason="Services and other sessions are protected";return false;}
    BOOL critical=FALSE;using Critical=BOOL(WINAPI*)(HANDLE,PBOOL);
    auto check=reinterpret_cast<Critical>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"IsProcessCritical"));
    if(!check || !check(process,&critical)||critical){reason="Critical/protected process";return false;}
    wchar_t path[32768]{};DWORD n=32768;
    if(!QueryFullProcessImageNameW(process,0,path,&n)||system_path(path)){reason="Windows process is protected";return false;}
    auto name=lower(utf8(path));
    for(auto term:{"defender","security","antivirus","firewall","sophos","crowdstrike","sentinel","kaspersky","bitdefender","headroom","csrss","lsass","winlogon"})
        if(name.find(term)!=std::string::npos){reason="Security and system software is protected";return false;}
    try{auto a=user(GetCurrentProcess()),b=user(process);if(!EqualSid(reinterpret_cast<TOKEN_USER*>(a.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid)){reason="Only this user's apps can be slept";return false;}}
    catch(const std::exception& e){reason=e.what();return false;}
    return true;
}
SleepSession::SleepSession(const Process& entry){
    std::string reason;if(!sleep_eligible(entry,reason))throw std::runtime_error(reason);
    SECURITY_ATTRIBUTES sa{sizeof sa,nullptr,TRUE};
    HANDLE parent=nullptr,target=nullptr,ready=nullptr,mapping=nullptr;
    DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&parent,SYNCHRONIZE,TRUE,0);
    target=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|0x0800,TRUE,entry.pid);                          
    stop_event=CreateEventW(&sa,TRUE,FALSE,nullptr);ready=CreateEventW(&sa,TRUE,FALSE,nullptr);
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(Shared),nullptr);
    struct Cleanup{HANDLE& a;HANDLE& b;HANDLE& c;HANDLE& d;~Cleanup(){for(auto h:{a,b,c,d})if(h)CloseHandle(h);}} cleanup{parent,target,ready,mapping};
    if(!parent||!target||!ready||!stop_event||!mapping){if(stop_event)CloseHandle(stop_event);stop_event=nullptr;throw std::runtime_error(error());}
    auto shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!shared)throw std::runtime_error(error());shared->status=0;
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);std::wstring command=quote(self)+L" --sleep-worker";
    HANDLE inherited[]={parent,target,stop_event,ready,mapping};
    for(auto h:inherited)command+=L" "+std::to_wstring(reinterpret_cast<UINT_PTR>(h));
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<BYTE> memory(size);
    startup.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)){UnmapViewOfFile(shared);throw std::runtime_error(error());}
    UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof inherited,nullptr,nullptr);
    PROCESS_INFORMATION info{};BOOL launched=CreateProcessW(self,&command[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&info);
    auto code=GetLastError();DeleteProcThreadAttributeList(startup.lpAttributeList);
    if(!launched){UnmapViewOfFile(shared);CloseHandle(stop_event);stop_event=nullptr;throw std::runtime_error(error(code));}
    child=info.hProcess;CloseHandle(info.hThread);
    DWORD wait=WaitForSingleObject(ready,5000);LONG status=shared->status;UnmapViewOfFile(shared);
    if(wait!=WAIT_OBJECT_0||status!=1){SetEvent(stop_event);WaitForSingleObject(child,5000);CloseHandle(child);child=nullptr;CloseHandle(stop_event);stop_event=nullptr;throw std::runtime_error("The process could not be safely suspended");}
}
SleepSession::~SleepSession(){
    if(stop_event)SetEvent(stop_event);
    if(child){WaitForSingleObject(child,5000);CloseHandle(child);}
    if(stop_event)CloseHandle(stop_event);
}
int sleep_worker(int argc,wchar_t** argv){
    if(argc<7)return 1;
    HANDLE handles[5]{};for(int i=0;i<5;++i)handles[i]=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(argv[i+2])));
    auto parent=handles[0],target=handles[1],stop=handles[2],ready=handles[3],mapping=handles[4];
    auto shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
    if(!shared)return 1;
    auto ntdll=GetModuleHandleW(L"ntdll.dll");auto suspend=reinterpret_cast<Suspend>(GetProcAddress(ntdll,"NtSuspendProcess")),resume=reinterpret_cast<Suspend>(GetProcAddress(ntdll,"NtResumeProcess"));
    bool slept=false;
    if(suspend&&resume&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT){slept=suspend(target)>=0;}
    shared->status=slept?1:-1;SetEvent(ready);
    if(slept){HANDLE waits[]={parent,target,stop};WaitForMultipleObjects(3,waits,FALSE,INFINITE);resume(target);}
    UnmapViewOfFile(shared);for(auto h:handles)if(h)CloseHandle(h);return slept?0:1;
}
}
