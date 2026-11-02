#include "tools.hpp"
#include "registry.hpp"
#include <shlobj.h>
#include <psapi.h>
#include <sddl.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>
namespace hr {
namespace {
struct Handle {
    HANDLE h;
    explicit Handle(HANDLE value=nullptr):h(value){}
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
unsigned long long ticks(FILETIME t){return (static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
bool under(std::wstring path,std::wstring root){
    std::transform(path.begin(),path.end(),path.begin(),towlower);
    std::transform(root.begin(),root.end(),root.begin(),towlower);
    if(root.empty())return false;
    if(root.back()!=L'\\')root+=L'\\';
    return path.compare(0,root.size(),root)==0;
}
std::vector<BYTE> owner(HANDLE process){
    HANDLE raw=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&raw))throw std::runtime_error(error());
    Handle token(raw);DWORD n=0;GetTokenInformation(token.h,TokenUser,nullptr,0,&n);
    std::vector<BYTE> bytes(n);
    if(!GetTokenInformation(token.h,TokenUser,bytes.data(),n,&n))throw std::runtime_error(error());
    return bytes;
}
std::string powershell(const std::wstring& script,DWORD timeout=90000){
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
    auto exe=std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
                                                                                  
                                                                                   
    auto output=data_dir()/(L"query-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".txt");
    auto literal=[](std::wstring s){size_t i=0;while((i=s.find(L'\'',i))!=std::wstring::npos){s.insert(i,1,L'\'');i+=2;}return L"'"+s+L"'";};
    auto command=L"$ErrorActionPreference='Stop'; try { & { "+script+L" } | Out-File -LiteralPath "+literal(output.wstring())+
        L" -Encoding utf8; exit 0 } catch { $_.Exception.Message | Out-File -LiteralPath "+literal(output.wstring())+L" -Encoding utf8; exit 1 }";
    DWORD result=run(exe.wstring(),{L"-NoProfile",L"-NonInteractive",L"-Command",command},timeout);
    std::ifstream stream(output,std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(stream),{}};stream.close();
    std::error_code ignored;std::filesystem::remove(output,ignored);
    if(text.compare(0,3,"\xef\xbb\xbf")==0)text.erase(0,3);
    if(result)throw std::runtime_error(text.empty()?"Windows package operation failed":text);
    return text;
}
const std::vector<std::string> app_allowlist={
    "Microsoft.BingNews","Microsoft.BingWeather","Microsoft.GetHelp","Microsoft.Getstarted",
    "Microsoft.MicrosoftSolitaireCollection","Microsoft.People","Microsoft.WindowsFeedbackHub",
    "Microsoft.YourPhone","Microsoft.ZuneMusic","Microsoft.ZuneVideo","MicrosoftTeams","MSTeams","Clipchamp.Clipchamp"
};
bool allowed_app(const std::string& name){return std::find(app_allowlist.begin(),app_allowlist.end(),name)!=app_allowlist.end();}
std::wstring literal(std::string s){
    auto value=wide(s);size_t i=0;
    while((i=value.find(L'\'',i))!=std::wstring::npos){value.insert(i,1,L'\'');i+=2;}
    return L"'"+value+L"'";
}
std::vector<std::string> split(const std::string& line,char separator){
    std::vector<std::string> out;size_t start=0;
    for(;;){auto end=line.find(separator,start);out.push_back(line.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}
    return out;
}
const std::vector<std::pair<std::wstring,std::wstring>> registry_allowlist={
    {L"Software\\Microsoft\\GameBar",L"AllowAutoGameMode"},
    {L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",L"AppCaptureEnabled"},
    {L"System\\GameConfigStore",L"GameDVR_Enabled"},
    {L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",L"SystemPaneSuggestionsEnabled"},
    {L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",L"SubscribedContent-338388Enabled"},
    {L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo",L"Enabled"}
};
std::string reg_state(const std::wstring& path,const std::wstring& value){
    HKEY key=nullptr;auto r=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_QUERY_VALUE,&key);
    if(r==ERROR_FILE_NOT_FOUND)return "-";check(r,"Open optimization setting");
    DWORD type=0,n=0;r=RegQueryValueExW(key,value.c_str(),nullptr,&type,nullptr,&n);
    if(r==ERROR_FILE_NOT_FOUND){RegCloseKey(key);return "-";}
    if(r){RegCloseKey(key);check(r,"Read optimization setting");}
    std::string bytes(n,'\0');
    r=RegQueryValueExW(key,value.c_str(),nullptr,&type,n?reinterpret_cast<BYTE*>(&bytes[0]):nullptr,&n);
    RegCloseKey(key);check(r,"Read optimization setting");
    return std::to_string(type)+":"+hex(bytes);
}
void write_state(const std::wstring& path,const std::wstring& value,const std::string& state){
    if(std::find(registry_allowlist.begin(),registry_allowlist.end(),std::make_pair(path,value))==registry_allowlist.end())throw std::runtime_error("Unrecognized optimization setting");
    HKEY key=nullptr;check(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr),"Open optimization setting");
    LONG r;
    if(state=="-"){r=RegDeleteValueW(key,value.c_str());if(r==ERROR_FILE_NOT_FOUND)r=ERROR_SUCCESS;}
    else{
        auto colon=state.find(':');if(colon==std::string::npos){RegCloseKey(key);throw std::runtime_error("Invalid registry recovery data");}
        auto bytes=unhex(state.substr(colon+1));DWORD type=static_cast<DWORD>(std::stoul(state.substr(0,colon)));
        r=RegSetValueExW(key,value.c_str(),0,type,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()));
    }
    RegCloseKey(key);check(r,"Write optimization setting");
}
void tweak(Store& store,size_t index,DWORD value){
    const auto& setting=registry_allowlist.at(index);
    auto before=reg_state(setting.first,setting.second);
    std::string bytes(reinterpret_cast<const char*>(&value),sizeof value);
    auto after=std::to_string(REG_DWORD)+":"+hex(bytes);
    if(before==after)return;
    registry_backup(store,HKEY_CURRENT_USER,setting.first);
    store.rows.push_back({"journal","registry",utf8(setting.first),before,after,utf8(setting.second)});store.save();
    write_state(setting.first,setting.second,after);
}
}
Process own_process(){
    FILETIME created{},exited{},k{},u{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&k,&u))throw std::runtime_error(error());
    return {GetCurrentProcessId(),L"NovaTune",0,0,ticks(created)};
}
double trim_ram(const Process& entry){
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_SET_QUOTA|PROCESS_VM_READ,FALSE,entry.pid));
    if(!process.h)throw std::runtime_error(error());
    FILETIME created{},exited{},k{},u{};
    if(!GetProcessTimes(process.h,&created,&exited,&k,&u) || ticks(created)!=entry.created)throw std::runtime_error("Process changed; select it again");
    if(entry.pid!=GetCurrentProcessId()){
        auto current=owner(GetCurrentProcess()),target=owner(process.h);
        if(!EqualSid(reinterpret_cast<TOKEN_USER*>(current.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(target.data())->User.Sid))throw std::runtime_error("Only your own apps can be trimmed");
        DWORD session=0,current_session=0;ProcessIdToSessionId(entry.pid,&session);ProcessIdToSessionId(GetCurrentProcessId(),&current_session);
        if(session!=current_session)throw std::runtime_error("Only apps in this session can be trimmed");
        wchar_t path[32768]{},windows[MAX_PATH]{};DWORD n=32768;
        if(!QueryFullProcessImageNameW(process.h,0,path,&n))throw std::runtime_error(error());
        GetWindowsDirectoryW(windows,MAX_PATH);
        if(under(path,windows))throw std::runtime_error("Windows processes are protected");
    }
    PROCESS_MEMORY_COUNTERS before{},after{};
    if(!GetProcessMemoryInfo(process.h,&before,sizeof before))throw std::runtime_error(error());
    if(!EmptyWorkingSet(process.h))throw std::runtime_error(error());
    if(!GetProcessMemoryInfo(process.h,&after,sizeof after))throw std::runtime_error(error());
    return before.WorkingSetSize>after.WorkingSetSize?(before.WorkingSetSize-after.WorkingSetSize)/1048576.0:0;
}
std::filesystem::path temp_root(){
    PWSTR value=nullptr;check(static_cast<DWORD>(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&value)),"Find user temp");
    std::filesystem::path path(value);CoTaskMemFree(value);return path/L"Temp";
}
std::vector<TempFile> scan_temp(const std::filesystem::path& requested){
    auto root=requested.empty()?temp_root():requested;
    auto base=std::filesystem::weakly_canonical(temp_root()),canonical=std::filesystem::weakly_canonical(root);
    if(canonical!=base && !under(canonical.wstring(),base.wstring()))throw std::runtime_error("Cleanup is restricted to your local Temp directory");
    std::vector<TempFile> out;
    FILETIME now{};GetSystemTimeAsFileTime(&now);auto cutoff=ticks(now)-24ULL*60*60*10000000;
    if(!std::filesystem::exists(root))return out;
    std::vector<std::filesystem::path> directories{root};
    while(!directories.empty()){
        auto dir=directories.back();directories.pop_back();
        DWORD attributes=GetFileAttributesW(dir.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
        std::error_code ec;
        for(std::filesystem::directory_iterator it(dir,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec && it!=end;it.increment(ec)){
            auto path=it->path();attributes=GetFileAttributesW(path.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
            if(attributes&FILE_ATTRIBUTE_DIRECTORY){directories.push_back(path);continue;}
            Handle file(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            if(file.h==INVALID_HANDLE_VALUE)continue;
            BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(file.h,&info) || info.nNumberOfLinks!=1 || ticks(info.ftLastWriteTime)>cutoff)continue;
            out.push_back({path,(static_cast<unsigned long long>(info.nFileSizeHigh)<<32)|info.nFileSizeLow,ticks(info.ftLastWriteTime),
                (static_cast<unsigned long long>(info.nFileIndexHigh)<<32)|info.nFileIndexLow,info.dwVolumeSerialNumber});
            if(out.size()>=20000)return out;
        }
    }
    return out;
}
CleanupResult clean_temp(const std::vector<TempFile>& files){
    CleanupResult out;
    auto root=std::filesystem::weakly_canonical(temp_root());
    FILETIME now{};GetSystemTimeAsFileTime(&now);auto cutoff=ticks(now)-24ULL*60*60*10000000;
    for(const auto& item:files){
        Handle file(CreateFileW(item.path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(file.h==INVALID_HANDLE_VALUE){++out.skipped;continue;}
        BY_HANDLE_FILE_INFORMATION info{};wchar_t final_path[32768]{};
        DWORD count=GetFinalPathNameByHandleW(file.h,final_path,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        std::wstring final=final_path;if(final.compare(0,4,L"\\\\?\\")==0)final.erase(0,4);
        auto identity=(static_cast<unsigned long long>(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
        if(!GetFileInformationByHandle(file.h,&info)){++out.skipped;continue;}
        identity=(static_cast<unsigned long long>(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
        if(!count || count>=32768 || !under(final,root.wstring()) ||
           (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) || info.nNumberOfLinks!=1 ||
           info.dwVolumeSerialNumber!=item.volume || identity!=item.identity || ticks(info.ftLastWriteTime)!=item.modified || ticks(info.ftLastWriteTime)>cutoff){
            ++out.skipped;continue;
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        if(SetFileInformationByHandle(file.h,FileDispositionInfo,&disposition,sizeof disposition)){++out.deleted;out.bytes+=item.size;}
        else ++out.skipped;
    }
    return out;
}
void apply_fps_profile(Store& store){
    tweak(store,0,1);tweak(store,1,0);tweak(store,2,0);switch_mode(store,"Gaming");
    store.log("FPS profile applied","Game Mode enabled; background Game DVR recording disabled. Use Undo for each setting.");
}
void reduce_suggestions(Store& store){
    tweak(store,3,0);tweak(store,4,0);tweak(store,5,0);
    store.log("Suggestions reduced","Windows content suggestions and advertising ID disabled for this user");
}
void undo_registry(const Row& row){
    auto current=reg_state(wide(row[2]),wide(row[5]));
    if(current==row[3])return;
    if(current!=row[4])throw std::runtime_error("Optimization setting changed outside NovaTune; refusing to overwrite");
    write_state(wide(row[2]),wide(row[5]),row[3]);
}
Records optional_apps(){
    std::wstring names=L"@(";bool first=true;
    for(const auto& name:app_allowlist){if(!first)names+=L",";names+=literal(name);first=false;}names+=L")";
    auto output=powershell(L"Get-AppxPackage | Where-Object { $_.Name -in "+names+
        L" -and -not $_.IsFramework -and -not $_.IsResourcePackage -and -not $_.NonRemovable } | ForEach-Object { $_.Name + [char]9 + $_.PackageFullName + [char]9 + $_.PackageFamilyName + [char]9 + $_.InstallLocation }");
    Records apps;std::istringstream stream(output);std::string line;
    while(std::getline(stream,line)){
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        auto fields=split(line,'\t');
        if(fields.size()!=4 || !allowed_app(fields[0]))throw std::runtime_error("Unexpected Windows package inventory response");
        apps.push_back(std::move(fields));
    }
    return apps;
}
void remove_optional_package(const Row& app){
    if(app.size()!=4 || !allowed_app(app[0]) || !std::regex_match(app[1],std::regex("[A-Za-z0-9_.-]+")))throw std::runtime_error("Package is not an allowed optional app");
    powershell(L"$p=@(Get-AppxPackage -Name "+literal(app[0])+L" | Where-Object { $_.PackageFullName -eq "+literal(app[1])+
        L" -and -not $_.IsFramework -and -not $_.IsResourcePackage -and -not $_.NonRemovable }); if ($p.Count -ne 1) { throw 'Package changed; refresh first' }; Remove-AppxPackage -Package $p[0].PackageFullName -ErrorAction Stop");
}
void remove_optional_app(Store& store,const Row& app){
    if(app.size()!=4 || !allowed_app(app[0]))throw std::runtime_error("Package is not in the optional-app catalog");
    if(!std::regex_match(app[1],std::regex("[A-Za-z0-9_.-]+")))throw std::runtime_error("Invalid package identity");
    store.rows.push_back({"journal","appx",app[0],app[3],app[2],app[1]});store.save();
    remove_optional_package(app);
    store.log("Optional app removed",app[0]);
}
void restore_optional_app(const Row& journal){
    if(!allowed_app(journal[2]))throw std::runtime_error("Unknown optional app");
    auto apps=optional_apps();
    if(std::any_of(apps.begin(),apps.end(),[&](const Row& app){return app[0]==journal[2];}))return;
    auto manifest=std::filesystem::path(wide(journal[3]))/L"AppxManifest.xml";
    if(std::filesystem::is_regular_file(manifest)){
        powershell(L"Add-AppxPackage -Register "+literal(utf8(manifest.wstring()))+L" -DisableDevelopmentMode -ErrorAction Stop");
    }else{
        if(!std::regex_match(journal[4],std::regex("[A-Za-z0-9_.-]+")))throw std::runtime_error("Invalid package family");
        shell(L"ms-windows-store://pdp/?PFN="+wide(journal[4]));
        throw std::runtime_error("Windows removed the package files. Reinstall from the Store page that was opened, then retry Undo.");
    }
}
std::vector<std::pair<unsigned long long,double>> LiveCapture::frames()const{std::lock_guard<std::mutex> lock(mutex);return {recent_frames.begin(),recent_frames.end()};}
std::string LiveCapture::game()const{std::lock_guard<std::mutex> lock(mutex);return captured_game;}
std::string LiveCapture::metric()const{std::lock_guard<std::mutex> lock(mutex);return captured_metric;}
LiveCapture::~LiveCapture(){stop();}
void LiveCapture::stop(){stopping=true;if(worker.joinable())worker.join();current=0;updated=0;}
double LiveCapture::fps()const{return GetTickCount64()-updated.load()>2500?0:current.load();}
std::string LiveCapture::error()const{std::lock_guard<std::mutex> lock(mutex);return problem;}
void LiveCapture::start(const std::string& executable,const std::string& game){
    stop();stopping=false;{std::lock_guard<std::mutex> lock(mutex);problem.clear();recent_frames.clear();captured_game=game;captured_metric.clear();}
    if(executable.empty() || !std::filesystem::is_regular_file(std::filesystem::path(wide(executable))))throw std::runtime_error("Select and save PresentMon.exe in Settings first");
    worker=std::thread([this,executable,game]{
        try{
            SECURITY_ATTRIBUTES sa{sizeof sa,nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
            if(!CreatePipe(&read,&write,&sa,0))throw std::runtime_error(hr::error());
            Handle input(read),output(write);SetHandleInformation(input.h,HANDLE_FLAG_INHERIT,0);
            Handle null(CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
            std::wstring exe=wide(executable),name=std::filesystem::path(wide(game)).filename().wstring();
            auto cmd=quote(exe)+L" --process_name "+quote(name)+L" --output_stdout --v1_metrics --no_console_stats --session_name HeadroomLive_"+std::to_wstring(GetCurrentProcessId());
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdOutput=output.h;startup.StartupInfo.hStdError=null.h;startup.StartupInfo.hStdInput=null.h;
            SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
            std::vector<BYTE> attributes(size);startup.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
            if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size))throw std::runtime_error(hr::error());
            HANDLE inherited[]={output.h,null.h};
            BOOL updated_list=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof inherited,nullptr,nullptr);
            PROCESS_INFORMATION raw{};
            BOOL created=updated_list && CreateProcessW(exe.c_str(),&cmd[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&raw);
            auto launch_error=GetLastError();DeleteProcThreadAttributeList(startup.lpAttributeList);
            if(!created)throw std::runtime_error(hr::error(launch_error));
            Handle process(raw.hProcess),thread(raw.hThread);CloseHandle(output.h);output.h=nullptr;
            struct StopChild {HANDLE process;~StopChild(){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,0);WaitForSingleObject(process,3000);}}} child{process.h};
            std::string buffer;std::deque<std::pair<ULONGLONG,double>> frames;
            FrameStream parser(utf8(name));
                                                                                  
            while(!stopping){
                DWORD available=0;if(!PeekNamedPipe(input.h,nullptr,0,nullptr,&available,nullptr))break;
                if(available){
                    char chunk[8192];DWORD read_count=0;
                    if(!ReadFile(input.h,chunk,std::min<DWORD>(available,sizeof chunk),&read_count,nullptr))break;
                    buffer.append(chunk,read_count);
                    for(;;){
                        auto newline=buffer.find('\n');if(newline==std::string::npos)break;
                        auto line=buffer.substr(0,newline);buffer.erase(0,newline+1);
                        if(!line.empty() && line.back()=='\r')line.pop_back();
                        try{
                            double interval=parser.consume(line);
                            if(interval>0){auto tick=GetTickCount64();frames.push_back({tick,interval});std::lock_guard<std::mutex> lock(mutex);recent_frames.push_back({tick,interval});if(captured_metric.empty())captured_metric=parser.metric();while(recent_frames.size()>10000||(!recent_frames.empty()&&tick-recent_frames.front().first>60000))recent_frames.pop_front();}
                        }catch(const std::exception&){continue;}
                    }
                    auto now=GetTickCount64();
                    while(!frames.empty() && now-frames.front().first>1000)frames.pop_front();
                    if(frames.size()>=2){
                        double total=0;for(const auto& frame:frames)total+=frame.second;
                        current=1000.0*frames.size()/total;updated=now;
                    }
                }else{
                    if(WaitForSingleObject(process.h,0)==WAIT_OBJECT_0){
                        DWORD code=0;GetExitCodeProcess(process.h,&code);
                        if(!stopping)throw std::runtime_error("PresentMon ended ("+std::to_string(code)+"). Check capture permissions and version.");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
                if(buffer.size()>1048576)throw std::runtime_error("Unexpected PresentMon output");
            }
        }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);problem=e.what();}
    });
}
}
