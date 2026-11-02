#include "windows.hpp"
#include "tools.hpp"
#include "input.hpp"
#include "registry.hpp"
#include "system_tools.hpp"
#include "autoruns.hpp"
#include "nvidia.hpp"
#include "memory.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <powrprof.h>
#include <powersetting.h>
#include <wbemidl.h>
#include <wuapi.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
namespace hr {
namespace {
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE v = nullptr) : h(v) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
};
struct Key {
    HKEY h = nullptr;
    ~Key() { if (h) RegCloseKey(h); }
};
struct Sc {
    SC_HANDLE h = nullptr;
    explicit Sc(SC_HANDLE v) : h(v) { if (!h) throw std::runtime_error(error()); }
    ~Sc() { CloseServiceHandle(h); }
};
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
struct Bstr {
    BSTR p;
    explicit Bstr(const wchar_t* s) : p(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(p); }
    operator BSTR() const { return p; }
};
void hrcheck(HRESULT r, const char* action) {
    if (FAILED(r)) throw std::runtime_error(std::string(action) + ": " + error(static_cast<DWORD>(r)));
}
struct Apartment {
    HRESULT result;
    Apartment() : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) { hrcheck(result, "COM initialization"); }
    ~Apartment() { CoUninitialize(); }
};
unsigned long long ticks(FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
std::string guid_text(const GUID& g) {
    wchar_t text[40]; StringFromGUID2(g, text, 40); return utf8(text);
}
GUID parse_guid(const std::string& text) {
    GUID g{}; hrcheck(CLSIDFromString(wide(text).c_str(), &g), "Invalid power plan"); return g;
}
constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::string property(IWbemClassObject* obj, const wchar_t* name) {
    VARIANT v; VariantInit(&v);
    std::string result;
    if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal) result = utf8(v.bstrVal);
    VariantClear(&v); return result;
}
bool under(std::wstring path, std::wstring root) {
    std::transform(path.begin(), path.end(), path.begin(), towlower);
    std::transform(root.begin(), root.end(), root.begin(), towlower);
    if (root.empty()) return false;
    if (root.back() != L'\\') root += L'\\';
    return path.compare(0, root.size(), root) == 0;
}
std::wstring env(const wchar_t* key) {
    DWORD n = GetEnvironmentVariableW(key, nullptr, 0);
    if (!n) return {};
    std::wstring s(n, L'\0'); GetEnvironmentVariableW(key, &s[0], n); s.resize(n-1); return s;
}
bool eligible(const Service& s) {
    if (s.start != SERVICE_AUTO_START && s.start != SERVICE_DEMAND_START && s.start != SERVICE_DISABLED) return false;
    std::string text = lower(s.name + " " + s.display + " " + s.path);
    for (const auto& word : {"microsoft", "windows", "defender", "security", "antivirus", "firewall", "endpoint",
                            "crowdstrike", "sentinel", "sophos", "mcafee", "kaspersky", "bitdefender",
                            "eset", "avast", "avg", "vpn", "anticheat", "anti-cheat"})
        if (text.find(word) != std::string::npos) return false;
    auto command = wide(s.path);
    std::wstring exe;
    if (!command.empty() && command[0] == L'"') {
        auto end = command.find(L'"', 1);
        if (end == std::wstring::npos) return false;
        exe = command.substr(1, end - 1);
    } else {
                                                             
        auto end = command.find(L' ');
        exe = command.substr(0, end);
    }
    if (exe.size() < 4 || lower(utf8(exe.substr(exe.size()-4))) != ".exe") return false;
    if (!under(exe, env(L"ProgramFiles")) && !under(exe, env(L"ProgramFiles(x86)"))) return false;
    DWORD attrs = GetFileAttributesW(exe.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    DWORD dummy = 0, n = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
    if (!n) return false;                                   
    std::vector<BYTE> data(n);
    if (!GetFileVersionInfoW(exe.c_str(), 0, n, data.data())) return false;
    struct Translation { WORD language, codepage; };
    Translation* tr = nullptr; UINT size = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &size) || size < sizeof(Translation)) return false;
    wchar_t key[100]; swprintf(key, 100, L"\\StringFileInfo\\%04x%04x\\CompanyName", tr[0].language, tr[0].codepage);
    wchar_t* company = nullptr;
    if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&company), &size) || !company || !*company) return false;
    auto maker = lower(utf8(company));
    for (const auto& word : {"microsoft", "security", "antivirus", "sophos", "mcafee", "kaspersky", "eset", "avast", "crowdstrike", "sentinel"})
        if (maker.find(word) != std::string::npos) return false;
    return true;
}
Service query_service(SC_HANDLE manager, const std::string& name) {
    Sc item(OpenServiceW(manager, wide(name).c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS));
    DWORD need = 0;
    QueryServiceConfigW(item.h, nullptr, 0, &need);
    if (!need) throw std::runtime_error(error());
    std::vector<BYTE> buffer(need);
    auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if (!QueryServiceConfigW(item.h, config, need, &need)) throw std::runtime_error(error());
    SERVICE_STATUS_PROCESS status{};
    if (!QueryServiceStatusEx(item.h, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof status, &need)) throw std::runtime_error(error());
    SERVICE_DELAYED_AUTO_START_INFO delay{};
    QueryServiceConfig2W(item.h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, reinterpret_cast<BYTE*>(&delay), sizeof delay, &need);
    Service s{name, utf8(config->lpDisplayName), utf8(config->lpBinaryPathName), config->dwStartType,
              status.dwCurrentState, config->dwStartType == SERVICE_AUTO_START && delay.fDelayedAutostart != FALSE, false};
    s.eligible = (config->dwServiceType & SERVICE_WIN32) && eligible(s);
    return s;
}
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring out(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n); return out;
}
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr); return out;
}
std::string error(DWORD code) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::string out = message ? utf8(message) : "Windows error " + std::to_string(code);
    if (message) LocalFree(message);
    return out;
}
void check(DWORD r, const char* action) { if (r) throw std::runtime_error(std::string(action) + ": " + error(r)); }
std::string now() {
    SYSTEMTIME t; GetLocalTime(&t); char out[32];
    snprintf(out, sizeof out, "%04u-%02u-%02u %02u:%02u:%02u", t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    return out;
}
std::filesystem::path migrate_app_folder(const std::filesystem::path& parent) {
    auto target=parent/L"NovaTune",legacy=parent/L"Headroom";
                                                                                   
    if(!std::filesystem::exists(target)&&std::filesystem::exists(legacy))std::filesystem::rename(legacy,target);
    std::filesystem::create_directories(target);return target;
}
std::filesystem::path data_dir() {
    PWSTR value = nullptr;
    hrcheck(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &value), "AppData");
    std::filesystem::path path(value); CoTaskMemFree(value);
    return migrate_app_folder(path);
}
std::string read_shared_file(const std::filesystem::path& path,size_t limit){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.h==INVALID_HANDLE_VALUE){auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return {};throw std::runtime_error("Unable to read data: "+error(code));}LARGE_INTEGER length{};if(!GetFileSizeEx(file.h,&length)||length.QuadPart<0||static_cast<unsigned long long>(length.QuadPart)>limit)throw std::runtime_error("Data file exceeds its read limit");std::string result(static_cast<size_t>(length.QuadPart),'\0');size_t offset=0;while(offset<result.size()){DWORD received=0,request=static_cast<DWORD>(std::min<size_t>(1024*1024,result.size()-offset));if(!ReadFile(file.h,result.data()+offset,request,&received,nullptr)||!received)throw std::runtime_error("Data file changed while reading");offset+=received;}return result;
}
void atomic_write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    auto temp = path; temp += L".tmp";
    { Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
      if (file.h == INVALID_HANDLE_VALUE) throw std::runtime_error(error());
      DWORD written = 0;
      if (!WriteFile(file.h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size() || !FlushFileBuffers(file.h))
          throw std::runtime_error("Unable to persist settings: " + error());
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error(error());
}
namespace {
std::string migrate_saved_path_text(std::string value,const std::vector<std::pair<std::string,std::string>>& paths,unsigned depth=0){
    if(depth<8&&value.rfind("HEADROOM 2\n",0)==0){auto records=decode(value);for(auto& row:records)for(auto& field:row)field=migrate_saved_path_text(field,paths,depth+1);return encode(records);}
    for(const auto& pair:paths){for(bool encoded:{false,true}){auto from=encoded?hex(pair.first):pair.first,to=encoded?hex(pair.second):pair.second;size_t at=0;while((at=value.find(from,at))!=std::string::npos){value.replace(at,from.size(),to);at+=to.size();}}}return value;
}
std::vector<std::pair<std::string,std::string>> migrated_path_prefixes(){
    std::vector<std::pair<std::string,std::string>> paths;
    for(auto id:{FOLDERID_RoamingAppData,FOLDERID_Videos,FOLDERID_ProgramFiles}){PWSTR raw=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&raw))){std::filesystem::path parent(raw);CoTaskMemFree(raw);auto old=parent/L"Headroom",next=parent/L"NovaTune";if(!std::filesystem::exists(old)&&std::filesystem::exists(next))paths.push_back({utf8(old.wstring())+"\\",utf8(next.wstring())+"\\"});}}
    return paths;
}
}
Store::Store(std::filesystem::path file) : path(file.empty() ? data_dir() / L"settings-v2.dat" : std::move(file)) {
    if (std::filesystem::exists(path)) {
        rows = decode(read_shared_file(path));
        for (const auto& row : rows) {
            const auto& kind = row[0];
            size_t required = kind == "setting" ? 3 : kind == "game" ? 3 : kind == "journal" ? 6 :
                              kind == "disabled-autorun" ? 3 : kind == "backup" ? 5 : kind == "stress" ? 10 : kind == "mapping" ? 5 : kind == "disabled" ? 4 : kind == "benchmark" ? 11 : kind == "history" ? 4 : 0;
            if (!required || row.size() != required) throw std::runtime_error("Damaged settings record; file preserved");
        }
        if(path.filename()==L"settings-v2.dat"&&path.parent_path().filename()==L"NovaTune"){
            auto prefixes=migrated_path_prefixes();bool changed=false;
            for(auto& row:rows)for(auto& field:row){auto updated=migrate_saved_path_text(field,prefixes);if(updated!=field){field=std::move(updated);changed=true;}}
            if(changed)save();
        }
    }
}
void Store::save() { atomic_write(path, encode(rows)); }
std::string Store::get(const std::string& key, const std::string& fallback) const {
    for (const auto& r : rows) if (r[0] == "setting" && r[1] == key) return r[2];
    return fallback;
}
void Store::set(const std::string& key, const std::string& value) {
    for (auto& r : rows) if (r[0] == "setting" && r[1] == key) { r[2] = value; return; }
    rows.push_back({"setting",key,value});
}
Records Store::of(const std::string& kind) const {
    Records result; for (const auto& r : rows) if (r[0] == kind) result.push_back(r); return result;
}
void Store::log(const std::string& action, const std::string& detail) {
    rows.push_back({"history",now(),action,detail});
    size_t count = 0;
    for (size_t i = rows.size(); i-- > 0;) if (rows[i][0] == "history" && ++count > 300) rows.erase(rows.begin()+i);
    save();
}
Sample sample() {
    static unsigned long long last_idle = 0, last_total = 0;
    static std::map<DWORD,std::pair<unsigned long long,unsigned long long>> previous;
    static unsigned long long last_wall = 0;
    Sample s;
    struct DiskCounters {
        PDH_HQUERY query=nullptr;PDH_HCOUNTER read=nullptr,write=nullptr;bool ready=false;
        DiskCounters(){
            if(PdhOpenQueryW(nullptr,0,&query)!=ERROR_SUCCESS)return;
            ready=PdhAddEnglishCounterW(query,L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",0,&read)==ERROR_SUCCESS &&
                  PdhAddEnglishCounterW(query,L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",0,&write)==ERROR_SUCCESS;
        }
        ~DiskCounters(){if(query)PdhCloseQuery(query);}
    };
    static DiskCounters disk;
    if(disk.ready && PdhCollectQueryData(disk.query)==ERROR_SUCCESS){
        PDH_FMT_COUNTERVALUE read{},write{};
        if(PdhGetFormattedCounterValue(disk.read,PDH_FMT_DOUBLE,nullptr,&read)==ERROR_SUCCESS && read.CStatus<=PDH_CSTATUS_NEW_DATA)s.disk_read=read.doubleValue/1048576.0;
        if(PdhGetFormattedCounterValue(disk.write,PDH_FMT_DOUBLE,nullptr,&write)==ERROR_SUCCESS && write.CStatus<=PDH_CSTATUS_NEW_DATA)s.disk_write=write.doubleValue/1048576.0;
    }
    FILETIME idle{}, kernel{}, user{}, wall{};
    GetSystemTimes(&idle,&kernel,&user); GetSystemTimeAsFileTime(&wall);
    auto total = ticks(kernel)+ticks(user), id = ticks(idle), current = ticks(wall);
    if (last_total && total > last_total) s.cpu = 100.0 * (1.0-static_cast<double>(id-last_idle)/(total-last_total));
    last_idle = id; last_total = total;
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof memory;
    if (!GlobalMemoryStatusEx(&memory)) throw std::runtime_error(error());
    s.total = memory.ullTotalPhys / 1073741824.0;
    s.used = (memory.ullTotalPhys-memory.ullAvailPhys) / 1073741824.0;
    SYSTEM_INFO sys{}; GetSystemInfo(&sys);
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if (snapshot.h == INVALID_HANDLE_VALUE) throw std::runtime_error(error());
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof entry;
    std::map<DWORD,std::pair<unsigned long long,unsigned long long>> next;
    if (Process32FirstW(snapshot.h, &entry)) do {
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,FALSE,entry.th32ProcessID));
        if (!process.h) continue;
        PROCESS_MEMORY_COUNTERS counters{};
        if (!GetProcessMemoryInfo(process.h,&counters,sizeof counters)) continue;
        FILETIME created{}, exited{}, k{}, u{};
        if (!GetProcessTimes(process.h,&created,&exited,&k,&u)) continue;
        auto time = ticks(k)+ticks(u), birth = ticks(created);
        double cpu = 0;
        auto found = previous.find(entry.th32ProcessID);
        if (found != previous.end() && found->second.second == birth && current > last_wall && time >= found->second.first)
            cpu = 100.0*(time-found->second.first)/(current-last_wall)/sys.dwNumberOfProcessors;
        next[entry.th32ProcessID] = {time,birth};
        s.processes.push_back({entry.th32ProcessID,entry.szExeFile,counters.WorkingSetSize/1048576.0,cpu,birth});
    } while (Process32NextW(snapshot.h,&entry));
    previous = std::move(next); last_wall = current;
    std::sort(s.processes.begin(),s.processes.end(),[](const Process& a,const Process& b){return a.ram>b.ram;});
    Com<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory.p)))) {
        Com<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(0,&adapter.p) == S_OK) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                std::wostringstream text; text << desc.Description << L" | Dedicated VRAM: " << desc.DedicatedVideoMemory/1048576
                    << L" MB | GPU utilization / VRAM usage: N/A";
                s.gpu = text.str();
            }
        }
    }
                                                                             
                                                                           
    struct Nvml {
        HMODULE module = nullptr;
        using Init = int (*)();
        using Shutdown = int (*)();
        using Device = int (*)(unsigned, void**);
        struct Util { unsigned gpu, memory; };
        struct Memory { unsigned long long total, free, used; };
        using ReadUtil = int (*)(void*, Util*);
        using ReadMemory = int (*)(void*, Memory*);
        using ReadName = int (*)(void*, char*, unsigned);
        using Temperature = int (*)(void*,unsigned,unsigned*);Temperature temperature=nullptr;
        Init init = nullptr; Shutdown shutdown = nullptr; Device device = nullptr;
        ReadUtil util = nullptr; ReadMemory memory = nullptr; ReadName name = nullptr;
        bool ready = false;
        Nvml() {
            module = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) {
                auto path = std::filesystem::path(env(L"ProgramFiles")) / L"NVIDIA Corporation/NVSMI/nvml.dll";
                module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            }
            if (!module) return;
            init = reinterpret_cast<Init>(GetProcAddress(module,"nvmlInit_v2"));
            shutdown = reinterpret_cast<Shutdown>(GetProcAddress(module,"nvmlShutdown"));
            device = reinterpret_cast<Device>(GetProcAddress(module,"nvmlDeviceGetHandleByIndex_v2"));
            util = reinterpret_cast<ReadUtil>(GetProcAddress(module,"nvmlDeviceGetUtilizationRates"));
            memory = reinterpret_cast<ReadMemory>(GetProcAddress(module,"nvmlDeviceGetMemoryInfo"));
            name = reinterpret_cast<ReadName>(GetProcAddress(module,"nvmlDeviceGetName"));
            temperature=reinterpret_cast<Temperature>(GetProcAddress(module,"nvmlDeviceGetTemperature"));
            ready = init && shutdown && device && util && memory && name && init() == 0;
        }
        ~Nvml() { if (ready) shutdown(); if (module) FreeLibrary(module); }
    };
    static Nvml nvml;
    if (nvml.ready) {
        void* device = nullptr; Nvml::Util util{}; Nvml::Memory memory{}; char name[128]{};
        if (!nvml.device(0,&device) && !nvml.util(device,&util) && !nvml.memory(device,&memory) && !nvml.name(device,name,sizeof name)) {
            std::wostringstream text;
            text << wide(name) << L" | GPU: " << util.gpu << L"% | VRAM: "
                 << memory.used/1048576 << L" / " << memory.total/1048576 << L" MB";
            unsigned temperature=0;if(nvml.temperature&&!nvml.temperature(device,0,&temperature)&&temperature<=150){s.gpu_temp=temperature;text<<L" / "<<temperature<<L" C";}
            s.gpu = text.str();s.gpu_util=util.gpu;s.vram_used=static_cast<double>(memory.used);s.vram_total=static_cast<double>(memory.total);
        }
    }
    if (s.gpu.empty()) s.gpu = L"GPU / VRAM telemetry unavailable";
    return s;
}
Records pc_info(){
    Records rows;wchar_t name[256]{};DWORD size=256;
    if(GetComputerNameW(name,&size))rows.push_back({"Device",utf8(name)});
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS){
        wchar_t cpu[512]{};DWORD bytes=sizeof cpu;
        if(RegQueryValueExW(key,L"ProcessorNameString",nullptr,nullptr,reinterpret_cast<BYTE*>(cpu),&bytes)==ERROR_SUCCESS)rows.push_back({"CPU",utf8(cpu)});
        RegCloseKey(key);
    }
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;GlobalMemoryStatusEx(&memory);
    std::ostringstream ram;ram<<std::fixed<<std::setprecision(1)<<memory.ullTotalPhys/1073741824.0<<" GB RAM / "<<GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)<<" logical CPUs";rows.push_back({"Memory",ram.str()});
    using Version=LONG(WINAPI*)(OSVERSIONINFOW*);
    auto get=reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof version;
    if(get && get(&version)==0)rows.push_back({"Windows",std::string(version.dwBuildNumber>=22000?"Windows 11":"Windows 10")+" / build "+std::to_string(version.dwBuildNumber)});
    Com<IDXGIFactory1> factory;
    if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory.p)))){
        Com<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(0,&adapter.p)==S_OK){DXGI_ADAPTER_DESC1 desc{};if(SUCCEEDED(adapter->GetDesc1(&desc)))rows.push_back({"GPU",utf8(desc.Description)});}
    }
    return rows;
}
void close_app(const Process& p) {
    auto name = lower(utf8(p.name));
    const std::set<std::string> protected_names = {"system","registry","wininit.exe","csrss.exe","lsass.exe","services.exe","smss.exe","explorer.exe","dwm.exe","winlogon.exe"};
    if (p.pid <= 4 || p.pid == GetCurrentProcessId() || protected_names.count(name)) throw std::runtime_error("This process is protected");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.pid));
    if (!process.h) throw std::runtime_error(error());
    FILETIME created{}, exited{}, k{}, u{};
    if (!GetProcessTimes(process.h,&created,&exited,&k,&u) || ticks(created) != p.created) throw std::runtime_error("Process changed; refresh before trying again");
    struct Context { DWORD pid; unsigned count; } context{p.pid,0};
    EnumWindows([](HWND window,LPARAM param)->BOOL {
        auto& c = *reinterpret_cast<Context*>(param);
        DWORD pid; GetWindowThreadProcessId(window,&pid);
        if (pid == c.pid && IsWindowVisible(window)) { PostMessageW(window,WM_CLOSE,0,0); ++c.count; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&context));
    if (!context.count) throw std::runtime_error("No visible app window; NovaTune will not force-terminate this process");
}
std::vector<Plan> plans() {
    std::vector<Plan> out;
    for (ULONG index=0;;++index) {
        GUID g{}; DWORD n=sizeof g;
        DWORD r=PowerEnumerate(nullptr,nullptr,nullptr,ACCESS_SCHEME,index,reinterpret_cast<BYTE*>(&g),&n);
        if (r == ERROR_NO_MORE_ITEMS) break;
        check(r,"Enumerate power plans");
        n=0; PowerReadFriendlyName(nullptr,&g,nullptr,nullptr,nullptr,&n);
        std::vector<BYTE> buffer(n+sizeof(wchar_t),0);
        check(PowerReadFriendlyName(nullptr,&g,nullptr,nullptr,buffer.data(),&n),"Power plan name");
        out.push_back({guid_text(g),reinterpret_cast<wchar_t*>(buffer.data())});
    }
    return out;
}
std::string active_plan() {
    GUID* g=nullptr; check(PowerGetActiveScheme(nullptr,&g),"Read active plan");
    auto text=guid_text(*g); LocalFree(g); return text;
}
void set_plan(const std::string& text) { GUID g=parse_guid(text); check(PowerSetActiveScheme(nullptr,&g),"Set power plan"); }
std::vector<Startup> startup() {
    std::vector<Startup> out;
    for (int machine=0;machine<2;++machine) {
        Key key;
        LONG r=RegOpenKeyExW(machine?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER,run_key,0,KEY_READ,&key.h);
        if (r == ERROR_FILE_NOT_FOUND) continue;
        check(r,"Read startup entries");
        DWORD names=0, data=0; check(RegQueryInfoKeyW(key.h,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&names,&data,nullptr,nullptr),"Read startup metadata");
        for (DWORD i=0;;++i) {
            std::vector<wchar_t> name(names+2);
            std::string bytes(data+2,'\0');
            DWORD nn=static_cast<DWORD>(name.size()), nd=static_cast<DWORD>(bytes.size()), type=0;
            r=RegEnumValueW(key.h,i,name.data(),&nn,nullptr,&type,reinterpret_cast<BYTE*>(&bytes[0]),&nd);
            if (r == ERROR_NO_MORE_ITEMS) break;
            check(r,"Read startup entry"); bytes.resize(nd);
            out.push_back({utf8(std::wstring(name.data(),nn)),machine?"Machine":"User",bytes,type});
        }
    }
    return out;
}
void disable_startup(const Startup& entry) {
    if (entry.scope != "User") throw std::runtime_error("Only user startup entries can be changed");
    Key key; check(RegOpenKeyExW(HKEY_CURRENT_USER,run_key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key.h),"Open startup");
    DWORD n=0,type=0;
    check(RegQueryValueExW(key.h,wide(entry.name).c_str(),nullptr,&type,nullptr,&n),"Recheck startup");
    std::string bytes(n,'\0');
    check(RegQueryValueExW(key.h,wide(entry.name).c_str(),nullptr,&type,reinterpret_cast<BYTE*>(&bytes[0]),&n),"Recheck startup");
    if (bytes != entry.bytes || type != entry.type) throw std::runtime_error("Startup entry changed; refresh first");
    check(RegDeleteValueW(key.h,wide(entry.name).c_str()),"Disable startup");
}
void restore_startup(const Startup& entry) {
    Key key; check(RegCreateKeyExW(HKEY_CURRENT_USER,run_key,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key.h,nullptr),"Open startup");
    DWORD type=0,n=0;
    auto r=RegQueryValueExW(key.h,wide(entry.name).c_str(),nullptr,&type,nullptr,&n);
    if (r != ERROR_FILE_NOT_FOUND) {
        check(r,"Check existing startup entry");
        throw std::runtime_error("Startup entry already exists; refusing to overwrite it");
    }
    check(RegSetValueExW(key.h,wide(entry.name).c_str(),0,entry.type,reinterpret_cast<const BYTE*>(entry.bytes.data()),static_cast<DWORD>(entry.bytes.size())),"Restore startup");
}
std::vector<Service> services() {
    Sc manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_ENUMERATE_SERVICE|SC_MANAGER_CONNECT));
    DWORD need=0,count=0,resume=0;
    EnumServicesStatusExW(manager.h,SC_ENUM_PROCESS_INFO,SERVICE_WIN32,SERVICE_STATE_ALL,nullptr,0,&need,&count,&resume,nullptr);
    if (!need) return {};
    std::vector<BYTE> buffer(need); resume=0;
    if (!EnumServicesStatusExW(manager.h,SC_ENUM_PROCESS_INFO,SERVICE_WIN32,SERVICE_STATE_ALL,buffer.data(),static_cast<DWORD>(buffer.size()),&need,&count,&resume,nullptr))
        throw std::runtime_error(error());
    auto items=reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    std::vector<Service> out;
    for (DWORD i=0;i<count;++i) {
        try { out.push_back(query_service(manager.h,utf8(items[i].lpServiceName))); }
        catch (const std::exception&) {                                                        }
    }
    std::sort(out.begin(),out.end(),[](const Service& a,const Service& b){return lower(a.display)<lower(b.display);});
    return out;
}
Service service(const std::string& name) {
    Sc manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT)); return query_service(manager.h,name);
}
void set_service(const Service& original,DWORD mode,bool delayed,DWORD expected,bool expected_delayed) {
    auto current=service(original.name);
    if (!current.eligible || current.path != original.path) throw std::runtime_error("Service is protected or changed; refresh first");
    if (current.start != expected || current.delayed != expected_delayed) throw std::runtime_error("Service changed outside NovaTune; refusing to overwrite it");
    if (mode != SERVICE_AUTO_START && mode != SERVICE_DEMAND_START && mode != SERVICE_DISABLED) throw std::runtime_error("Unsupported service startup type");
    Sc manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
    Sc item(OpenServiceW(manager.h,wide(original.name).c_str(),SERVICE_CHANGE_CONFIG));
    if (!ChangeServiceConfigW(item.h,SERVICE_NO_CHANGE,mode,SERVICE_NO_CHANGE,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr))
        throw std::runtime_error("Service change failed. Administrator rights may be required. "+error());
    if (mode != SERVICE_AUTO_START) return;
    SERVICE_DELAYED_AUTO_START_INFO info{delayed?TRUE:FALSE};
    if (!ChangeServiceConfig2W(item.h,SERVICE_CONFIG_DELAYED_AUTO_START_INFO,&info)) {
        auto detail = error();
        ChangeServiceConfigW(item.h,SERVICE_NO_CHANGE,current.start,SERVICE_NO_CHANGE,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
        SERVICE_DELAYED_AUTO_START_INFO previous{current.delayed?TRUE:FALSE};
        ChangeServiceConfig2W(item.h,SERVICE_CONFIG_DELAYED_AUTO_START_INFO,&previous);
        throw std::runtime_error("Delayed-start update failed; rollback attempted. Refresh before using Undo. "+detail);
    }
}
std::string service_mode(DWORD mode,bool delayed) {
    return mode==SERVICE_AUTO_START?(delayed?"Auto (delayed)":"Auto"):mode==SERVICE_DEMAND_START?"Manual":mode==SERVICE_DISABLED?"Disabled":"Protected";
}
Drivers drivers(bool updates) {
    Apartment apartment;
                                                                                    
    CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);
    Drivers result;
    Com<IWbemLocator> locator;
    hrcheck(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_IWbemLocator,reinterpret_cast<void**>(&locator.p)),"WMI locator");
    Com<IWbemServices> svc;
    hrcheck(locator->ConnectServer(Bstr(L"ROOT\\CIMV2"),nullptr,nullptr,nullptr,0,nullptr,nullptr,&svc.p),"WMI connection");
    hrcheck(CoSetProxyBlanket(svc.p,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE),"WMI security");
    Com<IEnumWbemClassObject> enumerator;
    hrcheck(svc->ExecQuery(Bstr(L"WQL"),Bstr(L"SELECT DeviceName,Manufacturer,DriverVersion,DriverDate FROM Win32_PnPSignedDriver WHERE DeviceClass='DISPLAY'"),WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&enumerator.p),"Driver query");
    for (;;) {
        Com<IWbemClassObject> item; ULONG returned=0;
        auto r=enumerator->Next(15000,1,&item.p,&returned);
        hrcheck(r,"Driver inventory");
        if (r == WBEM_S_TIMEDOUT) throw std::runtime_error("Driver inventory timed out");
        if (!returned) break;
        auto date=property(item.p,L"DriverDate");
        if (date.size()>=8) date=date.substr(0,4)+"-"+date.substr(4,2)+"-"+date.substr(6,2);
        result.installed.push_back({property(item.p,L"DeviceName"),property(item.p,L"Manufacturer"),property(item.p,L"DriverVersion"),date});
    }
    if (!updates) return result;
    try {
        Com<IUpdateSession> session;
        GUID clsid{};
        hrcheck(CLSIDFromProgID(L"Microsoft.Update.Session",&clsid),"Windows Update session");
        hrcheck(CoCreateInstance(clsid,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUpdateSession),reinterpret_cast<void**>(&session.p)),"Windows Update session");
        Com<IUpdateSearcher> searcher;
        hrcheck(session->CreateUpdateSearcher(&searcher.p),"Windows Update searcher");
        Com<ISearchResult> found;
        hrcheck(searcher->Search(Bstr(L"IsInstalled=0 and Type='Driver' and IsHidden=0"),&found.p),"Windows Update search");
        OperationResultCode status;
        hrcheck(found->get_ResultCode(&status),"Windows Update result");
        if (status != orcSucceeded) throw std::runtime_error("Windows Update search did not complete successfully");
        Com<IUpdateCollection> offers;
        hrcheck(found->get_Updates(&offers.p),"Windows Update offers");
        LONG count=0; hrcheck(offers->get_Count(&count),"Windows Update count");
        for (LONG i=0;i<count;++i) {
            Com<IUpdate> update; hrcheck(offers->get_Item(i,&update.p),"Windows Update item");
            BSTR title=nullptr;
            if (SUCCEEDED(update->get_Title(&title)) && title) { result.offers.push_back(utf8(title)); SysFreeString(title); }
        }
    } catch (const std::exception& e) { result.update_error=e.what(); }
    return result;
}
std::string vendor(const std::string& text) {
    auto name=lower(text);
    if (name.find("nvidia")!=std::string::npos) return "https://www.nvidia.com/en-us/drivers/";
    if (name.find("amd")!=std::string::npos || name.find("radeon")!=std::string::npos || name.find("advanced micro")!=std::string::npos) return "https://www.amd.com/en/support/download/drivers.html";
    if (name.find("intel")!=std::string::npos) return "https://www.intel.com/content/www/us/en/support/detect.html";
    return {};
}
std::wstring quote(const std::wstring& s) {
    std::wstring out=L"\""; unsigned slashes=0;
    for (auto c:s) {
        if (c == L'\\') ++slashes;
        else {
            out.append(c==L'"'?slashes*2+1:slashes,L'\\'); slashes=0; out+=c;
        }
    }
    out.append(slashes*2,L'\\'); return out+L'"';
}
void launch(const std::wstring& exe) {
    auto p=std::filesystem::path(exe);
    if (!std::filesystem::is_regular_file(p) || lower(utf8(p.extension().wstring()))!=".exe") throw std::runtime_error("Choose an existing Windows executable");
    std::wstring cmd=quote(exe); STARTUPINFOW startup{}; startup.cb=sizeof startup; PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(),&cmd[0],nullptr,nullptr,FALSE,0,nullptr,p.parent_path().c_str(),&startup,&process)) throw std::runtime_error(error());
    CloseHandle(process.hProcess); CloseHandle(process.hThread);
}
void shell(const std::wstring& target) {
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof info;info.fMask=SEE_MASK_FLAG_NO_UI;info.lpVerb=L"open";info.lpFile=target.c_str();info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info))throw std::runtime_error("Could not open this target: "+error());
}
std::wstring pick(HWND owner,const wchar_t* filter) {
    std::vector<wchar_t> file(32768,0);
    OPENFILENAMEW info{}; info.lStructSize=sizeof info; info.hwndOwner=owner; info.lpstrFilter=filter; info.lpstrFile=file.data(); info.nMaxFile=static_cast<DWORD>(file.size()); info.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&info)) return file.data();
    auto code=CommDlgExtendedError(); if (code) throw std::runtime_error("File dialog failed: "+std::to_string(code));
    return {};
}
DWORD run(const std::wstring& exe,const std::vector<std::wstring>& args,DWORD timeout) {
    std::wstring cmd=quote(exe); for (const auto& a:args) cmd+=L" "+quote(a);
    STARTUPINFOW startup{}; startup.cb=sizeof startup; PROCESS_INFORMATION p{};
    if (!CreateProcessW(exe.c_str(),&cmd[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&p)) throw std::runtime_error(error());
    Handle process(p.hProcess),thread(p.hThread);
    auto r=WaitForSingleObject(process.h,timeout);
    if (r != WAIT_OBJECT_0) {
        TerminateProcess(process.h,1); WaitForSingleObject(process.h,5000);
        throw std::runtime_error(r==WAIT_TIMEOUT?"External operation timed out":"Process wait failed");
    }
    DWORD code=0; if (!GetExitCodeProcess(process.h,&code)) throw std::runtime_error(error()); return code;
}
std::string capture(Store& store,const std::string& game,unsigned seconds) {
    if (seconds<10 || seconds>300) throw std::runtime_error("Benchmark duration must be 10-300 seconds");
    auto exe=store.get("presentmon");
    if (exe.empty() || !std::filesystem::is_regular_file(std::filesystem::path(wide(exe)))) throw std::runtime_error("Select PresentMon.exe in Settings first");
    auto output=data_dir()/L"benchmarks"; std::filesystem::create_directories(output);
    auto filename=output/(std::to_wstring(GetTickCount64())+L".csv");
    auto name=std::filesystem::path(wide(game)).filename().wstring();
    DWORD code=run(wide(exe),{L"--process_name",name,L"--timed",std::to_wstring(seconds),L"--terminate_after_timed",L"--output_file",filename.wstring(),L"--v1_metrics",L"--no_console_stats",L"--session_name",L"HeadroomBench_"+std::to_wstring(GetCurrentProcessId())},(seconds+40)*1000);
    if (code) throw std::runtime_error("PresentMon failed (exit "+std::to_string(code)+"). Check its version, game, and capture permissions.");
    return utf8(filename.wstring());
}
void switch_mode(Store& store,const std::string& mode) {
    if (mode!="Gaming" && mode!="Browsing" && mode!="Creating") throw std::runtime_error("Unknown mode");
    auto target=store.get("plan."+mode);
    if (!target.empty()) {
        auto before=active_plan();
        if (lower(before)!=lower(target)) {
            store.rows.push_back({"journal","power","",before,target,""}); store.save();
            set_plan(target);
        }
    }
    store.set("mode",mode); store.log("Mode switched",mode);
}
void change_service(Store& store,const Service& s,DWORD target) {
    auto current=service(s.name);
    if (!current.eligible || current.path!=s.path || current.start!=s.start || current.delayed!=s.delayed) throw std::runtime_error("Service changed or is protected; refresh first");
    bool delayed=false;
    if (current.start==target && current.delayed==delayed) return;
    auto before=std::to_string(current.start)+":"+std::to_string(current.delayed);
    auto after=std::to_string(target)+":"+std::to_string(delayed);
    registry_backup(store,HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\"+wide(s.name));
    store.rows.push_back({"journal","service",s.name,before,after,s.path}); store.save();
    set_service(current,target,delayed,current.start,current.delayed);
    store.log("Service startup changed",s.name+" -> "+service_mode(target,delayed));
}
void change_startup(Store& store,const Startup& s) {
    if (s.scope!="User") throw std::runtime_error("Machine startup entries are read-only");
    for (const auto& row:store.of("disabled")) if (row[1]==s.name) throw std::runtime_error("A backup already exists for this name; restore it first");
    registry_backup(store,HKEY_CURRENT_USER,run_key);
    store.rows.push_back({"journal","startup",s.name,std::to_string(s.type),hex(s.bytes),""}); store.save();
    disable_startup(s);
    store.rows.push_back({"disabled",s.name,std::to_string(s.type),hex(s.bytes)});
    store.log("Startup disabled",s.name);
}
void restore_saved_startup(Store& store,const Row& row) {
    restore_startup({row[1],"User",unhex(row[3]),static_cast<DWORD>(std::stoul(row[2]))});
    store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const Row& r){
        return (r[0]=="disabled" && r[1]==row[1]) || (r[0]=="journal" && r[1]=="startup" && r[2]==row[1]);
    }),store.rows.end());
    store.log("Startup restored",row[1]);
}
void undo_at(Store& store,size_t index) {
    if(index>=store.rows.size()||store.rows[index][0]!="journal")throw std::runtime_error("Select a saved change");
    auto row=store.rows[index];
    for(size_t i=index+1;i<store.rows.size();++i){const auto& later=store.rows[i];
        if(later[0]=="journal"&&later[1]==row[1]&&later[2]==row[2]&&((row[1]!="regvalue"&&row[1]!="registry")||later[5]==row[5]))throw std::runtime_error("Restore the newer change to this setting first");
    }
    if (row[1]=="power") {
        auto current=active_plan();
        if (lower(current)!=lower(row[3])) {
            if (lower(current)!=lower(row[4])) throw std::runtime_error("Power plan changed outside NovaTune");
            set_plan(row[3]);
        }
    } else if (row[1]=="service") {
        auto parse=[](const std::string& s){auto colon=s.find(':'); if(colon==std::string::npos)throw std::runtime_error("Invalid undo record");return std::make_pair(static_cast<DWORD>(std::stoul(s.substr(0,colon))),s.substr(colon+1)=="1");};
        auto before=parse(row[3]),after=parse(row[4]);
        auto current=service(row[2]);
        if (current.path!=row[5]) throw std::runtime_error("Service executable changed; refusing to restore");
        if (current.start!=before.first || current.delayed!=before.second)
            set_service(current,before.first,before.second,after.first,after.second);
    } else if (row[1]=="memory-service") {
        undo_memory_service(row);
    } else if (row[1]=="nv-cache") {
        undo_nvidia_cache(row);
    } else if (row[1]=="autorun") {
        undo_autorun(row);
        store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const Row& r){return r[0]=="disabled-autorun"&&r[1]==row[2];}),store.rows.end());
        auto found=std::find(store.rows.begin(),store.rows.end(),row);if(found!=store.rows.end())store.rows.erase(found);store.log("Change undone","autorun");return;
    } else if (row[1]=="plan-created" || row[1]=="power-setting") {
        undo_power_setting(row);
    } else if (row[1]=="regvalue") {
        undo_registry_value(row);
    } else if (row[1]=="mouse") {
        restore_mouse_settings(row);
    } else if (row[1]=="registry") {
        undo_registry(row);
    } else if (row[1]=="appx") {
        restore_optional_app(row);
    } else if (row[1]=="startup") {
        Startup backup{row[2],"User",unhex(row[4]),static_cast<DWORD>(std::stoul(row[3]))};
        auto all=startup();
        auto found=std::find_if(all.begin(),all.end(),[&](const Startup& s){return s.scope=="User" && s.name==backup.name;});
        if(found==all.end())restore_startup(backup);
        else if(found->bytes!=backup.bytes || found->type!=backup.type)throw std::runtime_error("Startup entry changed outside NovaTune");
        store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const Row& r){return r[0]=="disabled" && r[1]==backup.name;}),store.rows.end());
                                                              
        auto it=std::find(store.rows.begin(),store.rows.end(),row);
        if(it!=store.rows.end())store.rows.erase(it);
        store.log("Change undone","startup"); return;
    } else throw std::runtime_error("Unsupported undo record");
    store.rows.erase(store.rows.begin()+index); store.log("Change undone",row[1]);
}
void undo(Store& store){size_t index=store.rows.size();while(index&&store.rows[index-1][0]!="journal")--index;if(!index)throw std::runtime_error("Nothing to undo");undo_at(store,index-1);}

}
