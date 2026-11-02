#include <winsock2.h>
#include <ws2tcpip.h>
#include "gamer.hpp"
#include "registry.hpp"
#include "capture.hpp"
#include "game_sessions.hpp"
#include <iphlpapi.h>
#include <icmpapi.h>
#include <winioctl.h>
#include <setupapi.h>
#include <bcrypt.h>
#include <cfgmgr32.h>
#include <shlobj.h>
#include <powrprof.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <regex>
#include <sstream>
#include <set>
#include <stdexcept>
namespace hr {
namespace {
std::string registry_text(HKEY hive,const wchar_t* key,const wchar_t* name){DWORD bytes=0;if(RegGetValueW(hive,key,name,RRF_RT_REG_SZ,nullptr,nullptr,&bytes)!=ERROR_SUCCESS)return {};std::wstring value(bytes/2,L'\0');if(RegGetValueW(hive,key,name,RRF_RT_REG_SZ,nullptr,value.data(),&bytes)!=ERROR_SUCCESS)return {};value.resize(wcslen(value.c_str()));return utf8(value);}
bool key_exists(HKEY root,const wchar_t* path){HKEY key=nullptr;if(RegOpenKeyExW(root,path,0,KEY_READ,&key)!=ERROR_SUCCESS)return false;RegCloseKey(key);return true;}
std::string text_file(const std::filesystem::path& path,size_t limit=8*1024*1024){return read_shared_file(path,limit);}

std::filesystem::path known(REFKNOWNFOLDERID id){PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&value)))return {};std::filesystem::path path(value);CoTaskMemFree(value);return path;}
std::string format(double n,unsigned precision=1){std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<n;return out.str();}
std::string ping(const std::string& target,std::atomic<bool>& cancel){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Network API unavailable");struct Winsock{~Winsock(){WSACleanup();}} winsock;
    addrinfo hints{},*addresses=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
    auto host=target.empty()?"1.1.1.1":target.c_str();if(getaddrinfo(host,nullptr,&hints,&addresses))throw std::runtime_error("Cannot resolve this IPv4 target");auto destination=reinterpret_cast<sockaddr_in*>(addresses->ai_addr)->sin_addr.S_un.S_addr;freeaddrinfo(addresses);
    HANDLE icmp=IcmpCreateFile();if(icmp==INVALID_HANDLE_VALUE)throw std::runtime_error(error());struct Close{HANDLE h;~Close(){IcmpCloseHandle(h);}} close{icmp};
    std::vector<double> times;unsigned sent=0;std::ostringstream out;out<<"CONNECTION QUALITY / "<<host<<"\n12 ICMP samples, 750 ms timeout each. ICMP is a diagnostic, not the game's own traffic.\n\n";
    for(unsigned i=0;i<12&&!cancel;++i){char payload[]="NovaTune connection probe";std::vector<BYTE> reply(sizeof(ICMP_ECHO_REPLY)+sizeof(payload)+8);++sent;
        DWORD count=IcmpSendEcho(icmp,destination,payload,sizeof payload,nullptr,reply.data(),static_cast<DWORD>(reply.size()),750);
        auto echo=reinterpret_cast<ICMP_ECHO_REPLY*>(reply.data());if(count&&echo->Status==IP_SUCCESS){times.push_back(echo->RoundTripTime);out<<i+1<<": "<<echo->RoundTripTime<<" ms\n";}else out<<i+1<<": no successful echo\n";Sleep(100);
    }
    out<<"\nSent: "<<sent<<"  received: "<<times.size()<<"  loss: "<<format(sent?100.0*(sent-times.size())/sent:0)<<"%\n";
    if(!times.empty()){double total=std::accumulate(times.begin(),times.end(),0.0),jitter=0;for(size_t i=1;i<times.size();++i)jitter+=std::abs(times[i]-times[i-1]);out<<"Average: "<<format(total/times.size())<<" ms  min/max: "<<*std::min_element(times.begin(),times.end())<<" / "<<*std::max_element(times.begin(),times.end())<<" ms\nSuccessive received-sample jitter: "<<format(times.size()>1?jitter/(times.size()-1):0)<<" ms\n";}
    out<<"A blocked/rate-limited ICMP response does not prove game packet loss. Test the router and a reachable remote target separately.";return out.str();
}
std::string displays(){
    std::ostringstream out;out<<"DISPLAY SETUP\n\n";DISPLAY_DEVICEW device{};device.cb=sizeof device;
    for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&device,0);++i){if(!(device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))continue;DEVMODEW current{};current.dmSize=sizeof current;
        if(!EnumDisplaySettingsW(device.DeviceName,ENUM_CURRENT_SETTINGS,&current))continue;out<<utf8(device.DeviceString)<<" / "<<utf8(device.DeviceName)<<"\n"<<current.dmPelsWidth<<" x "<<current.dmPelsHeight<<" @ "<<current.dmDisplayFrequency<<" Hz, "<<current.dmBitsPerPel<<" bit\nPosition "<<current.dmPosition.x<<", "<<current.dmPosition.y<<"\n";
        std::set<DWORD> rates;DEVMODEW mode{};mode.dmSize=sizeof mode;for(DWORD n=0;EnumDisplaySettingsW(device.DeviceName,n,&mode);++n)if(mode.dmPelsWidth==current.dmPelsWidth&&mode.dmPelsHeight==current.dmPelsHeight)rates.insert(mode.dmDisplayFrequency);
        out<<"Reported refresh rates at this resolution: ";for(auto rate:rates)out<<rate<<" ";out<<"Hz\n";if(!rates.empty()&&current.dmDisplayFrequency<*rates.rbegin())out<<"Suggestion: a higher reported refresh rate exists. Check display/cable support in Windows Settings before switching.\n";out<<"\n";
    }
    out<<"VRR/FreeSync/G-SYNC availability is not inferred from refresh rate. Check the monitor OSD and driver controls.";return out.str();
}
std::string devices(bool usb){
    HDEVINFO set=SetupDiGetClassDevsW(nullptr,usb?L"USB":nullptr,nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);if(set==INVALID_HANDLE_VALUE)throw std::runtime_error(error());struct Close{HDEVINFO h;~Close(){SetupDiDestroyDeviceInfoList(h);}} close{set};
    std::ostringstream out;out<<(usb?"USB DEVICE / PARENT-HUB MAP":"CONNECTED DEVICE INVENTORY")<<"\n\n";SP_DEVINFO_DATA device{};device.cbSize=sizeof device;
    for(DWORD i=0;SetupDiEnumDeviceInfo(set,i,&device);++i){wchar_t name[1024]{},id[2048]{},parent_id[2048]{};if(!SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_FRIENDLYNAME,nullptr,reinterpret_cast<PBYTE>(name),sizeof name,nullptr))SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_DEVICEDESC,nullptr,reinterpret_cast<PBYTE>(name),sizeof name,nullptr);
        SetupDiGetDeviceInstanceIdW(set,&device,id,2048,nullptr);DEVINST parent{};if(CM_Get_Parent(&parent,device.DevInst,0)==CR_SUCCESS)CM_Get_Device_IDW(parent,parent_id,2048,0);out<<utf8(name)<<"\n  ID: "<<utf8(id)<<"\n  Parent: "<<utf8(parent_id)<<"\n\n";
    }return out.str();
}
std::string storage_health(){
    std::ostringstream out;out<<"STORAGE HEALTH / READ ONLY\nUnsupported values stay unavailable; a successful prediction query is not a full health guarantee.\n\n";
    for(unsigned i=0;i<32;++i){auto name=L"\\\\.\\PhysicalDrive"+std::to_wstring(i);HANDLE disk=CreateFileW(name.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(disk==INVALID_HANDLE_VALUE)continue;out<<"PhysicalDrive"<<i<<"\n";
        STORAGE_PROPERTY_QUERY query{};query.PropertyId=StorageDeviceProperty;query.QueryType=PropertyStandardQuery;std::vector<BYTE> data(4096);DWORD bytes=0;
        if(DeviceIoControl(disk,IOCTL_STORAGE_QUERY_PROPERTY,&query,sizeof query,data.data(),static_cast<DWORD>(data.size()),&bytes,nullptr)){auto descriptor=reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(data.data());if(descriptor->ProductIdOffset&&descriptor->ProductIdOffset<bytes)out<<"Model: "<<reinterpret_cast<const char*>(data.data()+descriptor->ProductIdOffset)<<"\n";}
        STORAGE_PREDICT_FAILURE prediction{};if(DeviceIoControl(disk,IOCTL_STORAGE_PREDICT_FAILURE,nullptr,0,&prediction,sizeof prediction,&bytes,nullptr))out<<"Failure prediction: "<<(prediction.PredictFailure?"DRIVE REPORTS FAILURE RISK":"not predicting failure")<<"\n";else out<<"Failure prediction unavailable on this device/driver.\n";
        out<<"Temperature: not exposed by this query.\n\n";CloseHandle(disk);
    }return out.str();
}
std::string events(const std::string& kind){
    auto log=data_dir()/L"logs"/(L"events-"+std::to_wstring(GetTickCount64())+L".xml");std::wstring query=kind=="memory"?L"*[System[Provider[@Name='Microsoft-Windows-MemoryDiagnostics-Results']]]":L"*[System[(Level=1 or Level=2) and TimeCreated[timediff(@SystemTime)<=604800000]]]";
    auto result=command_output(system_exe(L"wevtutil.exe").wstring(),{L"qe",kind=="memory"?L"System":L"Application",L"/q:"+query,L"/rd:true",L"/c:40",L"/f:Text"},log,30000);return "WINDOWS EVENT EVIDENCE\nThese records can include non-game applications and do not prove a root cause.\n\n"+result.output;
}
bool unsafe_root(const std::filesystem::path& root){
    if(!root.is_absolute()||root==root.root_path()||root.native().rfind(L"\\\\",0)==0)return true;auto lower_root=lower(utf8(root.lexically_normal().wstring()));
    for(auto path:{system_exe(L"cmd.exe").parent_path().parent_path(),known(FOLDERID_ProgramFiles),known(FOLDERID_ProgramFilesX86),data_dir()})if(!path.empty()){auto prefix=lower(utf8(path.wstring()));if(lower_root==prefix||lower_root.rfind(prefix+"\\",0)==0)return true;}return false;
}
bool reparse(const std::filesystem::path& path){auto attributes=GetFileAttributesW(path.c_str());return attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT);}
void safe_components(const std::filesystem::path& path){auto p=path.root_path();for(const auto& part:path.relative_path()){p/=part;if(std::filesystem::exists(p)&&reparse(p))throw std::runtime_error("Linked/reparse folders are excluded");}}
}
std::string readiness(const std::string& game){
    std::ostringstream out;out<<"PRE-GAME READINESS\n\n";SYSTEM_POWER_STATUS power{};if(GetSystemPowerStatus(&power)){out<<"Power: "<<(power.ACLineStatus==1?"plugged in":power.ACLineStatus==0?"battery":"unknown")<<"\n";if(power.BatteryLifePercent<=100)out<<"Battery: "<<static_cast<unsigned>(power.BatteryLifePercent)<<"%\n";}
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(GlobalMemoryStatusEx(&memory))out<<"Available RAM (includes reclaimable cache): "<<format(memory.ullAvailPhys/1073741824.0)<<" GB / "<<format(memory.ullTotalPhys/1073741824.0)<<" GB total\n";
    bool restart=key_exists(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending")||key_exists(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired");
    out<<"Known pending-restart flags: "<<(restart?"present; save work and restart when appropriate":"none detected by these checks")<<"\n";
    for(const auto& drive:drives()){out<<utf8(drive.root)<<" free: "<<format(drive.free/1073741824.0)<<" / "<<format(drive.total/1073741824.0)<<" GB";if(drive.total&&drive.free<drive.total/10)out<<"  LOW SPACE";out<<"\n";}
    if(!game.empty())out<<"Selected game exists: "<<(std::filesystem::is_regular_file(wide(game))?"yes":"no")<<"\n";
    out<<"\nNo settings changed. Storage thresholds are reminders, not measured FPS limits.";return out.str();
}
std::string compatibility(){
    std::ostringstream out;out<<"COMPATIBILITY\n\n";for(const auto& row:pc_info())out<<row[0]<<": "<<row[1]<<"\n";
    out<<"\nCapture: desktop WIC/Media Foundation; protected/exclusive-fullscreen content may be unavailable.\nPresentMon: separate executable and permissions required.\nController telemetry: exposed device/driver data only; no virtual-gamepad output driver.\nThermals: supported sensor APIs only; no estimated CPU temperature.\nRecovery: exact-state checks; changes made by other utilities can block restore.\nWindows-user data: current Windows account's AppData folder; no shared game-save folders by default.\nRegistry changes and deep system tools require elevation. Hardware-specific capabilities are checked at use time.";return out.str();
}
Records discover_games(const std::filesystem::path& override_root){
    std::set<std::filesystem::path> libraries;Records result;auto steam=override_root.empty()?std::filesystem::path(wide(registry_text(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath"))):override_root;
    if(!steam.empty()){
        libraries.insert(steam);auto vdf=text_file(steam/L"steamapps/libraryfolders.vdf");std::regex paths("\\\"path\\\"\\s*\\\"([^\\\"]+)\\\"");
        for(std::sregex_iterator it(vdf.begin(),vdf.end(),paths),end;it!=end;++it){auto path=(*it)[1].str();size_t n=0;while((n=path.find("\\\\",n))!=std::string::npos)path.replace(n,2,"\\");libraries.insert(std::filesystem::path(wide(path)));}
    }
    for(const auto& library:libraries){std::error_code ec;auto apps=library/L"steamapps";
        for(std::filesystem::directory_iterator it(apps,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec)){
            if(lower(utf8(it->path().extension().wstring()))!=".acf")continue;auto data=text_file(it->path());std::smatch id,name,folder;
            if(std::regex_search(data,id,std::regex("\\\"appid\\\"\\s*\\\"([0-9]+)\\\""))&&std::regex_search(data,name,std::regex("\\\"name\\\"\\s*\\\"([^\\\"]+)\\\""))&&std::regex_search(data,folder,std::regex("\\\"installdir\\\"\\s*\\\"([^\\\"]+)\\\""))){
                auto root=apps/L"common"/wide(folder[1].str());if(std::filesystem::is_directory(root))result.push_back({"Steam",id[1].str(),name[1].str(),utf8(root.wstring()),"steam://rungameid/"+id[1].str()});
            }
        }
    }
    auto manifests=known(FOLDERID_ProgramData)/L"Epic/EpicGamesLauncher/Data/Manifests";std::error_code ec;
    for(std::filesystem::directory_iterator it(manifests,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec)){
        if(it->path().extension()!=L".item")continue;auto data=text_file(it->path());std::smatch name,location,exe;
        if(std::regex_search(data,name,std::regex("\\\"DisplayName\\\"\\s*:\\s*\\\"([^\\\"]+)\\\""))&&std::regex_search(data,location,std::regex("\\\"InstallLocation\\\"\\s*:\\s*\\\"([^\\\"]+)\\\""))&&std::regex_search(data,exe,std::regex("\\\"LaunchExecutable\\\"\\s*:\\s*\\\"([^\\\"]+)\\\""))){auto path=location[1].str();size_t n=0;while((n=path.find("\\\\",n))!=std::string::npos)path.replace(n,2,"\\");auto root=std::filesystem::path(wide(path));auto binary=root/wide(exe[1].str());if(std::filesystem::is_regular_file(binary))result.push_back({"Epic","",name[1].str(),utf8(root.wstring()),utf8(binary.wstring())});}
    }
    return result;
}
namespace {
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE&&h)CloseHandle(h);}};
std::string digest_file(const std::filesystem::path& path,bool allow_delete=false){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<BYTE> object;
    struct Close{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Close(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} close{algorithm,hash};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 provider unavailable");DWORD size=0,bytes=0;if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof size,&bytes,0)<0)throw std::runtime_error("Hash setup failed");object.resize(size);
    if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0)throw std::runtime_error("Hash setup failed");
    Handle file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|(allow_delete?FILE_SHARE_DELETE:0),nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};if(file.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock snapshot file");BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file.h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked snapshots are excluded");
    std::vector<BYTE> data(1024*1024);DWORD read=0;for(;;){if(!ReadFile(file.h,data.data(),static_cast<DWORD>(data.size()),&read,nullptr))throw std::runtime_error("Hash read failed");if(!read)break;if(BCryptHashData(hash,data.data(),read,0)<0)throw std::runtime_error("Hash failed");}
    std::string digest(32,'\0');if(BCryptFinishHash(hash,reinterpret_cast<PUCHAR>(digest.data()),32,0)<0)throw std::runtime_error("Hash failed");return hex(digest);
}
std::string final_name(HANDLE h){std::wstring result(32768,L'\0');auto size=GetFinalPathNameByHandleW(h,result.data(),static_cast<DWORD>(result.size()),FILE_NAME_NORMALIZED);if(!size||size>=result.size())throw std::runtime_error("Cannot verify file location");result.resize(size);if(result.rfind(L"\\\\?\\",0)==0)result.erase(0,4);return lower(utf8(result));}
void scoped_copy(const std::filesystem::path& source,const std::filesystem::path& target,const std::filesystem::path& scope,const std::filesystem::path& source_scope,bool replace){
    safe_components(source);safe_components(target.parent_path());Handle input{CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(input.h==INVALID_HANDLE_VALUE)throw std::runtime_error("File is busy or inaccessible: "+utf8(source.filename().wstring()));BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(input.h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1)throw std::runtime_error("Linked or unsupported files are excluded");
    auto root=lower(utf8(source_scope.lexically_normal().wstring()));if(final_name(input.h).rfind(root+"\\",0)!=0)throw std::runtime_error("Source path changed");
    auto temporary=target;temporary+=L".headroom-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp";
    Handle output{CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};if(output.h==INVALID_HANDLE_VALUE)throw std::runtime_error(error());
    auto target_root=lower(utf8(scope.lexically_normal().wstring()));if(final_name(output.h).rfind(target_root+"\\",0)!=0)throw std::runtime_error("Destination left the approved folder");
    std::vector<char> data(1024*1024);DWORD read=0,written=0;bool ok=true;unsigned long long copied=0;for(;;){if(!ReadFile(input.h,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)){ok=false;break;}if(!read)break;copied+=read;if(copied>2ULL*1024*1024*1024){ok=false;break;}if(!WriteFile(output.h,data.data(),read,&written,nullptr)||written!=read){ok=false;break;}}
    if(read||!ok||!FlushFileBuffers(output.h)){CloseHandle(output.h);output.h=INVALID_HANDLE_VALUE;DeleteFileW(temporary.c_str());throw std::runtime_error("File copy failed; original kept");}
    CloseHandle(output.h);output.h=INVALID_HANDLE_VALUE;if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0))){DeleteFileW(temporary.c_str());throw std::runtime_error(error());}
}
void valid_relative(const std::filesystem::path& relative){if(relative.empty()||relative.is_absolute()||relative.has_root_name())throw std::runtime_error("Invalid relative file path");for(const auto& part:relative){auto name=part.wstring();if(name.empty()||name==L"."||name==L".."||name.back()==L'.'||name.back()==L' '||name.find_first_of(L"<>:\"|?*")!=std::wstring::npos)throw std::runtime_error("Unsafe relative file component");auto base=lower(utf8(part.stem().wstring()));if(base=="con"||base=="prn"||base=="aux"||base=="nul"||(base.size()==4&&(base.rfind("com",0)==0||base.rfind("lpt",0)==0)&&base[3]>='1'&&base[3]<='9'))throw std::runtime_error("Windows device paths are excluded");}}
std::filesystem::path snapshot_base(){auto root=data_dir()/L"snapshots";safe_components(root);std::filesystem::create_directories(root);return root;}
}
std::filesystem::path snapshot_folder(const std::filesystem::path& original,const std::string& label,std::atomic<bool>& cancel,bool dedicated_mod,bool strict){
    safe_components(original);auto source=std::filesystem::weakly_canonical(original);if(dedicated_mod)validate_mod_folder(source);if((unsafe_root(source)&&!dedicated_mod)||!std::filesystem::is_directory(source))throw std::runtime_error("Choose a specific personal save/configuration/mod folder outside Windows and NovaTune data");safe_components(source);
    static std::atomic<unsigned long long> sequence{0};auto destination=snapshot_base()/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence));if(!std::filesystem::create_directory(destination))throw std::runtime_error("Snapshot name collision");struct Incomplete{std::filesystem::path folder;bool committed=false;~Incomplete(){if(!committed){std::error_code ec;std::filesystem::remove_all(folder,ec);}}} cleanup{destination};std::filesystem::create_directory(destination/L"files");
    Records manifest{{"snapshot","1",label,utf8(source.wstring())}};unsigned long long total=0;std::error_code ec;
    for(std::filesystem::recursive_directory_iterator it(source,ec),end;!ec&&it!=end;it.increment(ec)){
        if(cancel)throw std::runtime_error("Backup canceled; incomplete snapshot discarded; originals preserved");if(reparse(it->path())){if(dedicated_mod||strict)throw std::runtime_error("Backup contains linked/cloud-placeholder entries; choose fully local files. Originals were preserved.");if(it->is_directory())it.disable_recursion_pending();continue;}if(!it->is_regular_file())continue;
        auto size=it->file_size();total+=size;if(total>2ULL*1024*1024*1024||manifest.size()>5000)throw std::runtime_error("Snapshot exceeds 2 GB / 5,000-file limit; choose a smaller folder");
        auto relative=std::filesystem::relative(it->path(),source);auto target=destination/L"files"/relative;std::filesystem::create_directories(target.parent_path());scoped_copy(it->path(),target,destination,source,false);
        manifest.push_back({"file",utf8(relative.wstring()),std::to_string(std::filesystem::file_size(target)),digest_file(target)});
    }
    if(ec)throw std::runtime_error("Folder changed or became unreadable; incomplete backup discarded");atomic_write(destination/L"manifest.dat",encode(manifest));cleanup.committed=true;return destination;
}
std::string restore_snapshot(const std::filesystem::path& selected,const std::filesystem::path& original,std::atomic<bool>& cancel){
    auto snapshot=std::filesystem::weakly_canonical(selected),base=snapshot_base();if(snapshot.parent_path()!=base)throw std::runtime_error("Choose a snapshot made by this Windows user's NovaTune");safe_components(snapshot);
    auto manifest=decode(text_file(snapshot/L"manifest.dat"));if(manifest.empty()||manifest[0].size()!=4||manifest[0][0]!="snapshot"||manifest[0][1]!="1"||manifest.size()>5001)throw std::runtime_error("Invalid snapshot manifest");
    auto target=std::filesystem::weakly_canonical(original);if(unsafe_root(target))throw std::runtime_error("Unsafe restore destination");safe_components(target);
    if(cancel)throw std::runtime_error("Restore canceled before any changes");std::set<std::string> paths;unsigned long long bytes=0;
    for(size_t i=1;i<manifest.size();++i){const auto& row=manifest[i];if(row.size()!=4||row[0]!="file")throw std::runtime_error("Invalid file record");std::filesystem::path relative=wide(row[1]);valid_relative(relative);if(!paths.insert(lower(row[1])).second)throw std::runtime_error("Duplicate snapshot destination; nothing restored");auto file=snapshot/L"files"/relative;safe_components(file);auto size=std::filesystem::file_size(file);if((bytes+=size)>2ULL*1024*1024*1024||std::to_string(size)!=row[2]||reparse(file)||digest_file(file)!=row[3])throw std::runtime_error("Snapshot integrity check failed; nothing restored");}
    std::string baseline;if(std::filesystem::is_directory(target))baseline=utf8(snapshot_folder(target,"Before restore",cancel,false,true).wstring());else std::filesystem::create_directories(target);
    unsigned restored=0;for(size_t i=1;i<manifest.size();++i){if(cancel)throw std::runtime_error("Restore stopped after "+std::to_string(restored)+" files. Before-restore snapshot: "+baseline);auto relative=std::filesystem::path(wide(manifest[i][1]));auto output=target/relative;std::filesystem::create_directories(output.parent_path());scoped_copy(snapshot/L"files"/relative,output,target,snapshot/L"files",true);if(digest_file(output)!=manifest[i][3])throw std::runtime_error("Restored file verification failed. Before-restore snapshot: "+baseline);++restored;}
    return "Restored "+std::to_string(restored)+" verified files. Extra destination files were preserved.\nBefore-restore snapshot: "+(baseline.empty()?"destination was new":baseline);
}
void validate_backup_folder(const std::filesystem::path& original){safe_components(original);auto root=std::filesystem::weakly_canonical(original);if(unsafe_root(root)||!std::filesystem::is_directory(root))throw std::runtime_error("Choose a specific existing personal save folder outside Windows, Program Files and NovaTune data");}
void validate_mod_folder(const std::filesystem::path& original){
    safe_components(original);auto root=std::filesystem::weakly_canonical(original);if(!root.is_absolute()||root==root.root_path()||root.native().rfind(L"\\\\",0)==0||!std::filesystem::is_directory(root))throw std::runtime_error("Choose an existing dedicated local mod folder");auto name=lower(utf8(root.filename().wstring()));const std::set<std::string> allowed={"mods","mod","addons","add-ons","plugins","custom"};if(!allowed.count(name))throw std::runtime_error("Choose a dedicated folder named Mods, Mod, Addons, Add-ons, Plugins or Custom; never a game or Windows root");auto path=lower(utf8(root.wstring()));for(auto protected_root:{system_exe(L"cmd.exe").parent_path().parent_path(),data_dir()}){auto prefix=lower(utf8(protected_root.wstring()));if(path==prefix||path.rfind(prefix+"\\",0)==0)throw std::runtime_error("Windows and NovaTune data are protected");}
}
namespace {
using FileMap=std::map<std::string,std::string>;
FileMap mod_tree(const std::filesystem::path& root){validate_mod_folder(root);FileMap result;unsigned long long total=0;std::error_code ec;for(std::filesystem::recursive_directory_iterator it(root,ec),end;!ec&&it!=end;it.increment(ec)){if(reparse(it->path()))throw std::runtime_error("Linked mod entries are excluded");if(!it->is_regular_file())continue;if(result.size()>=5000||(total+=it->file_size())>2ULL*1024*1024*1024)throw std::runtime_error("Mod folder exceeds 2 GB / 5,000 files");auto relative=std::filesystem::relative(it->path(),root);result.emplace(utf8(relative.wstring()),digest_file(it->path()));}if(ec)throw std::runtime_error("Mod folder could not be fully read");return result;}

FileMap snapshot_tree(const std::filesystem::path& snapshot,std::filesystem::path& original){
    auto selected=std::filesystem::weakly_canonical(snapshot);if(selected.parent_path()!=snapshot_base())throw std::runtime_error("Choose a snapshot created by this Windows account");safe_components(selected);auto rows=decode(text_file(selected/L"manifest.dat"));if(rows.empty()||rows.size()>5001||rows[0].size()!=4||rows[0][0]!="snapshot"||rows[0][1]!="1")throw std::runtime_error("Invalid mod snapshot manifest");original=wide(rows[0][3]);validate_mod_folder(original);FileMap files;unsigned long long total=0;for(size_t i=1;i<rows.size();++i){auto& row=rows[i];if(row.size()!=4||row[0]!="file")throw std::runtime_error("Invalid mod snapshot file record");std::filesystem::path relative=wide(row[1]);if(relative.empty()||relative.is_absolute()||relative.has_root_name())throw std::runtime_error("Invalid relative mod path");for(const auto& component:relative)if(component==L"."||component==L"..")throw std::runtime_error("Mod snapshot path escapes its folder");valid_relative(relative);auto file=selected/L"files"/relative;safe_components(file);if((total+=std::filesystem::file_size(file))>2ULL*1024*1024*1024||digest_file(file)!=row[3]||std::to_string(std::filesystem::file_size(file))!=row[2]||!files.emplace(row[1],row[3]).second)throw std::runtime_error("Mod snapshot integrity check failed; nothing switched");}return files;
}
std::string tree_fingerprint(const FileMap& files){Records records;for(const auto& pair:files)records.push_back({pair.first,pair.second});return encode(records);}
void remove_mod_file(const std::filesystem::path& path,const std::filesystem::path& root,const std::string& digest){
    safe_components(path);Handle file{CreateFileW(path.c_str(),DELETE|GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};if(file.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Mod file is busy");BY_HANDLE_FILE_INFORMATION information{};if(!GetFileInformationByHandle(file.h,&information)||information.nNumberOfLinks!=1||(information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||final_name(file.h).rfind(lower(utf8(root.wstring()))+"\\",0)!=0||digest_file(path,true)!=digest)throw std::runtime_error("Mod file changed or left its approved folder");FILE_DISPOSITION_INFO disposition{TRUE};if(!SetFileInformationByHandle(file.h,FileDispositionInfo,&disposition,sizeof disposition))throw std::runtime_error(error());
}
std::filesystem::path mod_jobs(){auto root=data_dir()/L"mod-switches";std::filesystem::create_directories(root);return root;}
}
ModPlan plan_mod_switch(const std::filesystem::path& snapshot){ModPlan plan;plan.snapshot=std::filesystem::weakly_canonical(snapshot);std::filesystem::path source;auto desired=snapshot_tree(plan.snapshot,source);plan.target=std::filesystem::weakly_canonical(source);auto existing=mod_tree(plan.target);plan.fingerprint=tree_fingerprint(existing);for(const auto& file:desired){auto found=existing.find(file.first);if(found==existing.end()||found->second!=file.second)plan.changes.push_back({file.first,found==existing.end()?"":found->second,file.second});}for(const auto& file:existing)if(!desired.count(file.first))plan.changes.push_back({file.first,file.second,""});return plan;}
std::string apply_mod_switch(const ModPlan& plan,std::atomic<bool>& cancel,const std::function<bool()>& can_write){
    auto reviewed=plan_mod_switch(plan.snapshot);if(reviewed.target!=plan.target||reviewed.fingerprint!=plan.fingerprint||reviewed.changes!=plan.changes)throw std::runtime_error("Mod files changed since review; preview the switch again");if(cancel||(can_write&&!can_write()))throw std::runtime_error("Mod switch deferred/canceled before changing files");if(plan.changes.empty())return "Mod folder already matches this profile.";
    auto baseline=snapshot_folder(plan.target,"Before mod switch",cancel,true);static std::atomic<unsigned> sequence{0};auto journal=mod_jobs()/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence)+L".dat");Records record{{"mod-switch","1","prepared",utf8(plan.target.wstring()),utf8(baseline.wstring()),utf8(plan.snapshot.wstring())}};atomic_write(journal,encode(record));std::vector<Row> changed;
    try{for(const auto& row:plan.changes){if(cancel||(can_write&&!can_write()))throw std::runtime_error("Mod switch stopped; rolling back only its own file changes");auto relative=std::filesystem::path(wide(row[0])),target=plan.target/relative;if(row[1].empty()){if(std::filesystem::exists(target))throw std::runtime_error("A new mod file appeared after review");}else if(digest_file(target)!=row[1])throw std::runtime_error("Mod file changed after review");record.push_back({"file",row[0],row[1],row[2],"armed"});atomic_write(journal,encode(record));changed.push_back(row);if(row[2].empty())remove_mod_file(target,plan.target,row[1]);else{safe_components(target.parent_path());std::filesystem::create_directories(target.parent_path());scoped_copy(plan.snapshot/L"files"/relative,target,plan.target,plan.snapshot/L"files",!row[1].empty());if(digest_file(target)!=row[2])throw std::runtime_error("Mod copy verification failed");}record.back()[4]="applied";atomic_write(journal,encode(record));}if(!plan_mod_switch(plan.snapshot).changes.empty())throw std::runtime_error("Mod folder changed during switching; restoring only this operation's changes");record[0][2]="completed";atomic_write(journal,encode(record));return "Switched "+std::to_string(changed.size())+" reviewed files. Clean/before-switch recovery snapshot: "+utf8(baseline.wstring());
    }catch(const std::exception& e){bool conflict=false;for(auto it=changed.rbegin();it!=changed.rend();++it){auto relative=std::filesystem::path(wide((*it)[0])),target=plan.target/relative;try{bool exists=std::filesystem::exists(target);if(((*it)[1].empty()&&!exists)||(!(*it)[1].empty()&&exists&&digest_file(target)==(*it)[1]))continue;if((*it)[2].empty()){if(std::filesystem::exists(target))throw std::runtime_error("External file appeared");}else if(digest_file(target)!=(*it)[2])throw std::runtime_error("External mod change preserved");if((*it)[1].empty())remove_mod_file(target,plan.target,(*it)[2]);else{std::filesystem::create_directories(target.parent_path());scoped_copy(baseline/L"files"/relative,target,plan.target,baseline/L"files",true);}}catch(const std::exception&){conflict=true;}}record[0][2]=conflict?"rollback-conflict":"rolled-back";atomic_write(journal,encode(record));throw std::runtime_error(std::string(e.what())+"\nBefore-switch snapshot: "+utf8(baseline.wstring())+(conflict?"\nAn external change was preserved; inspect recovery snapshot.":"\nApplied file changes were rolled back."));}
}
std::string mod_switch_history(){std::ostringstream out;out<<"MOD SWITCH RECOVERY\nUse a before-switch snapshot as a mod profile and preview its exact changes before restoring.\n\n";for(const auto& entry:std::filesystem::directory_iterator(mod_jobs()))try{auto rows=decode(text_file(entry.path()));if(!rows.empty()&&rows[0].size()==6)out<<rows[0][2]<<" / "<<rows[0][3]<<"\nBefore: "<<rows[0][4]<<"\nTarget: "<<rows[0][5]<<"\n\n";}catch(const std::exception&){out<<"Unreadable record preserved: "<<entry.path().filename().string()<<"\n";}return out.str();}
Records automatic_backup_versions(const std::string& game){Records result;auto index=data_dir()/L"automatic-backups.dat";if(!std::filesystem::exists(index))return result;auto rows=decode(text_file(index));for(auto it=rows.rbegin();it!=rows.rend();++it){const auto& row=*it;if(row.size()!=4||row[0]!="automatic-save")throw std::runtime_error("Automatic backup index is unreadable; old data preserved");if(lower(row[1])!=lower(game))continue;auto snapshot=std::filesystem::path(wide(row[3]));safe_components(snapshot);if(snapshot.parent_path()!=snapshot_base())throw std::runtime_error("Backup index is outside the private folder");auto manifest=decode(text_file(snapshot/L"manifest.dat"));if(manifest.empty()||manifest[0].size()!=4||manifest[0][0]!="snapshot")throw std::runtime_error("Backup manifest is unavailable");auto version=row;version.push_back(manifest[0][3]);result.push_back(std::move(version));}return result;}
std::string automatic_backup(const std::string& game,const std::filesystem::path& folder,unsigned retention,std::atomic<bool>& cancel){
    if(game.empty()||retention<1||retention>20)throw std::runtime_error("Automatic backup retention must be 1 to 20 versions");static std::mutex jobs;std::lock_guard<std::mutex> lock(jobs);unsigned long long identity=1469598103934665603ULL;for(unsigned char ch:utf8(data_dir().wstring())){identity^=ch;identity*=1099511628211ULL;}Handle owner{CreateMutexW(nullptr,FALSE,(L"Local\\Headroom.AutomaticBackup."+std::to_wstring(identity)).c_str())};if(!owner.h)throw std::runtime_error(error());auto acquired=WaitForSingleObject(owner.h,0);if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED)throw std::runtime_error("Another automatic save backup is running");struct Unlock{HANDLE h;~Unlock(){ReleaseMutex(h);}} unlock{owner.h};auto index_file=data_dir()/L"automatic-backups.dat";Records index;if(std::filesystem::exists(index_file)){index=decode(text_file(index_file));for(const auto& row:index)if(row.size()!=4||row[0]!="automatic-save")throw std::runtime_error("Automatic backup index is unreadable; old records preserved");}auto snapshot=snapshot_folder(folder,"Automatic game save: "+game,cancel,false,true);index.push_back({"automatic-save",game,now(),utf8(snapshot.wstring())});atomic_write(index_file,encode(index));if(cancel)return utf8(snapshot.wstring());unsigned kept=0;for(size_t i=index.size();i-->0;)if(lower(index[i][1])==lower(game)){if(++kept<=retention)continue;if(cancel)break;auto old=std::filesystem::path(wide(index[i][3]));safe_components(old);if(old.parent_path()!=snapshot_base())throw std::runtime_error("Backup index left the private snapshot folder; nothing pruned");auto manifest=decode(text_file(old/L"manifest.dat"));if(manifest.empty()||manifest[0].size()!=4||manifest[0][0]!="snapshot"||manifest[0][2]!="Automatic game save: "+game)throw std::runtime_error("Old backup identity changed; nothing pruned");std::filesystem::remove_all(old);index.erase(index.begin()+i);atomic_write(index_file,encode(index));}return utf8(snapshot.wstring());
}
std::vector<GamerTool> gamer_tools(){return {
    {"overlay-editor","Overlay layout editor","Gaming","Open the native HUD editor to customize position, size, opacity, colors, layout and real resource metrics.","","",false},
    {"capture-privacy","Recording privacy controls","Capture","Choose Windows cursor visibility and a capture region before recording/screenshots. A canceled or invalid selection never switches to fullscreen. Stop an active NovaTune recording before changing scope.","Cursor on or off","full or x,y,width,height (Select opens region picker)",false},
    {"crop-image","Crop a screenshot","Capture","Choose a PNG, select image pixels in the app preview, then create a new cropped PNG. The original is never overwritten.","Source PNG file","",false},
    {"trim-clip","Trim and export a clip","Capture","Export a selected MP4 time range to a new H.264/AAC file. Enter start,end in seconds, up to ten minutes. Mono/stereo audio is preserved; HDR and original bitrate are not. Originals are never overwritten.","Source MP4 file","Start,end seconds (for example 5,20)",false},
    {"capture-files","Captured files","Capture","Lists local PNG/MP4 captures. Crop and clip export create new files; originals remain in the capture folder.","","",false},
    {"maintenance-history","Maintenance history","Diagnostics","Shows the most recent 300 local operation records, including failures and recorded skipped-file counts. Reports stay on this Windows account.","","",false},
    {"favorite-game","Favorite game for tray","Gaming","Pins or unpins an existing library game in the tray launcher. This does not launch it or change system settings.","Game executable, or selected game","on or off",false},
    {"stutters","Stutter detective","Gaming","Analyze up to 60 seconds / 10,000 valid live PresentMon intervals and nearby resource samples. Capture-receive timing and roughly two-second resource sampling support possible associations, not proof of a root cause.","","",false},
    {"benchmark-notes","Benchmark scene notes","Gaming","Save repeatable scene, graphics, route and duration notes for the game selected in Games. Future saved benchmarks capture these notes and available environment metadata.","Scene name for selected game","Resolution / graphics / route / duration notes",false},
    {"performance-target","Performance targets","Gaming","Save average and 1% low FPS goals for the game selected in Games. This evaluates recorded results; it does not force an FPS cap or promise a gain.","Average FPS goal, 1 to 2000","Optional 1% low goal, no higher than average",false},
    {"benchmark-report","Before / after report","Gaming","Compare the newest same-game, same-metric captures and show frame-consistency changes, goals and scene comparability. Different scenes cannot establish a tweak's effect.","","",false},
    {"update-impact","Update impact tracking","Gaming","Compare the environment/driver fingerprints saved with comparable captures. Changes are shown as context and are not automatically blamed for a performance difference.","","",false},
    {"game-profile","Automatic game session profile","Gaming","Opt in this exact executable to local session history and crash-safe temporary priority. Competitive uses High, Streaming Above Normal, other presets Normal. Original priority is restored on game exit or NovaTune crash; no security setting is changed.","Game executable, or selected game","competitive / streaming / laptop / quiet / everyday",true},
    {"profile-disable","Disable automatic game profile","Gaming","Stops this game's active session and restores its temporary priority/background apps. Its recorded history is kept.","Game executable, or selected game","",true},
    {"background-sleep","Game background-app sleep list","Gaming","Temporarily suspends only the explicitly listed app executables during this game's opted-in session. Their own watchdogs resume them after game/session exit or NovaTune crash. Critical, Windows, security and other-user processes remain protected.","Game executable, or selected game","Background .exe paths separated by semicolons; empty clears",true},
    {"sessions","Session history","Gaming","Saved opted-in game sessions, duration context, sampled resource peaks and notes. Sampled overlay FPS is not presented as a full per-frame benchmark.","","",false},
    {"timeline","Background activity timeline","Gaming","Local snapshots of the five busiest other apps by sampled CPU during opted-in games, with working-set memory. Per-process network and disk attribution are not inferred from system counters.","","",false},
    {"crash-recovery","Recover interrupted sessions","Diagnostics","Shows unfinished temporary priority guards, then retries safe recovery. Process creation time, owner lifetime and current priority are checked; externally changed priorities are preserved.","","",true},
    {"controller","Controller calibration lab","Input","Live actual axes/triggers and battery information. Leave sticks untouched to inspect center drift. This visualizes input; it does not rewrite controller firmware or suppress native gamepad input.","","",false},
    {"controller-history","Controller connection history","Input","Local connection/disconnection history. Xbox slots and Sony HID reports depend on exposed driver data; no button presses are saved.","","",false},
    {"controller-alerts","Controller battery alerts","Input","Enable or disable low/empty battery notifications for controllers exposing battery data. Unknown values never generate a fabricated low-battery alert.","on or off","",false},
    {"deadzone","System-wide remapper dead zone","Input","Changes NovaTune right-stick-to-mouse mappings across Windows. Native controller axes are unchanged.","","Fraction from 0 to 0.8 (default 0.27)",false},
    {"thermal","GPU thermal alerts","Hardware","Opt-in high-temperature alerts from supported NVIDIA telemetry only. Unsupported GPU/CPU temperatures remain unavailable. Threshold alerts do not prove throttling.","on or off","Threshold Celsius, 50 to 110",false},
    {"readiness","Pre-game readiness","Gaming","Checks power, available memory, known restart flags and drive free space without changing settings.","","",false},
    {"network","Connection quality","Gaming","Twelve IPv4 ICMP probes measure received-sample latency, jitter and loss. A blocked echo is not proof of game packet loss.","Target host/IP (default 1.1.1.1)","",false},
    {"display","Display setup checker","Hardware","Shows current modes and reported refresh rates at the current resolution. No automatic mode change.","","",false},
    {"usb","USB device map","Hardware","Lists connected USB devices and their parent device IDs. Driver/hub relationships are reported, not guessed.","","",false},
    {"storage-health","Storage health","Hardware","Read-only drive identity and supported failure-prediction queries. Unsupported health values and temperatures stay unavailable.","","",false},
    {"crashes","Crash evidence","Diagnostics","Recent Application event errors. These may include non-games; timestamps and event records are evidence, not a diagnosis.","","",false},
    {"memory-results","Memory diagnostic results","Diagnostics","Reads Windows MemoryDiagnostics results from the System event log.","","",false},
    {"memory-launch","Schedule memory diagnostics","Diagnostics","Opens Windows Memory Diagnostic. Its Windows UI asks when to restart; NovaTune does not restart the PC.","","",false},
    {"compatibility","Compatibility checker","Diagnostics","Reports machine details and feature limitations without fabricating sensor support.","","",false},
    {"backup","Versioned save / config backup","Backups","Copy a selected folder into a verified, versioned local snapshot. Linked files are skipped; limit 2 GB / 5,000 files. Close the game first.","Source folder","Snapshot label",false},
    {"snapshots","Snapshot library","Backups","Lists completed local snapshots, their original folders and file counts. Incomplete snapshots are excluded.","","",false},
    {"restore","Restore selected snapshot","Backups","Verifies every saved file, snapshots the existing destination, then replaces matching files. Extra destination files remain. Close the game first.","Snapshot folder","Destination folder",true},
    {"discover","Discover installed games","Gaming","Scans Steam library manifests and Epic installed manifests. Review detected paths before adding games; no executable is guessed from a folder name.","Optional Steam folder","",false},
    {"sensitivity","Mouse sensitivity converter","Input","Preserves physical cm/360 with explicit DPI and game yaw values. Enter: source DPI,sensitivity,yaw. Target: DPI,yaw. This does not measure input latency or change games.","Example: 800,1,0.022","Example: 1600,0.022",false},
    {"streaming","Streaming readiness assistant","Capture","Reports actual resource/power headroom and current recorder constraints. Hardware encoder support is not assumed.","","",false},
    {"laptop","Laptop gaming assistant","Hardware","Battery, AC power and memory/storage readiness. Vendor fan/thermal limits are not overridden.","","",false},
    {"local-help","Local diagnostic assistant","Diagnostics","Explains current readiness findings locally using deterministic rules. No data leaves this PC and no generative diagnosis is invented.","","",false},
    {"diagnostic-export","Privacy-reviewed diagnostic export","Diagnostics","Preview the same compatibility/readiness report first. Export only that report to a new file; process lists, usernames, serial numbers and save contents are omitted.","New destination .txt file","",true}
};}
std::string gamer_report(const std::string& id,const std::string& first,const std::string& second,std::atomic<bool>& cancel){
    if(id=="capture-files")return capture_gallery();
    if(id=="sessions")return session_history();if(id=="timeline")return session_history(true);if(id=="crash-recovery")return interrupted_sessions();
    if(id=="readiness"||id=="laptop"||id=="local-help")return readiness(first);
    if(id=="network")return ping(first,cancel);if(id=="display")return displays();if(id=="usb")return devices(true);if(id=="storage-health")return storage_health();if(id=="crashes")return events("crashes");if(id=="memory-results")return events("memory");if(id=="compatibility")return compatibility();
    if(id=="sensitivity"){
        auto parse=[](std::string text){std::replace(text.begin(),text.end(),',',' ');std::istringstream input(text);std::vector<double> values;double value;while(input>>value){if(!std::isfinite(value)||value<=0)throw std::runtime_error("DPI, sensitivity and yaw must be positive finite numbers");values.push_back(value);}if(!input.eof())throw std::runtime_error("Enter comma-separated numbers");return values;};auto a=parse(first),b=parse(second);if(a.size()!=3||b.size()!=2)throw std::runtime_error("Source needs DPI,sensitivity,yaw; target needs DPI,yaw");double cm=360*2.54/(a[0]*a[1]*a[2]),sensitivity=a[0]*a[1]*a[2]/(b[0]*b[1]);if(!std::isfinite(cm)||!std::isfinite(sensitivity))throw std::runtime_error("Values exceed supported range");return "Matched horizontal sensitivity: "+format(sensitivity,6)+"\nPhysical distance: "+format(cm,3)+" cm per 360 degrees.\nUse the actual yaw coefficient for each game; scoped/ADS multipliers require separate conversion.";
    }
    if(id=="discover"){auto games=discover_games(first.empty()?std::filesystem::path():std::filesystem::path(wide(first)));std::ostringstream out;out<<games.size()<<" detected installed games\n\n";for(const auto& row:games)out<<row[2]<<" ["<<row[0]<<"]\n"<<row[3]<<"\nLaunch: "<<row[4]<<"\n\n";return out.str();}
    if(id=="snapshots"){std::ostringstream out;for(const auto& folder:std::filesystem::directory_iterator(snapshot_base())){auto text=text_file(folder.path()/L"manifest.dat");if(text.empty())continue;try{auto rows=decode(text);if(rows.empty()||rows[0].size()!=4)continue;out<<rows[0][2]<<" / "<<rows.size()-1<<" files\nOriginal: "<<rows[0][3]<<"\nSnapshot: "<<utf8(folder.path().wstring())<<"\n\n";}catch(const std::exception&){}}return out.str().empty()?"No completed snapshots yet.":out.str();}
    if(id=="streaming")return readiness()+"\n\n"+encoder_report();
    if(id=="diagnostic-export"){std::ostringstream out;out<<"PRIVACY PREVIEW / HARDWARE AND READINESS ONLY\n";for(const auto& row:pc_info())if(row[0]!="Device")out<<row[0]<<": "<<row[1]<<"\n";return out.str()+"\n"+readiness();}
    throw std::runtime_error("This action requires Run after reviewing its description");
}
std::string gamer_action(const std::string& id,const std::string& first,const std::string& second,std::atomic<bool>& cancel){
    if(id=="crash-recovery")return interrupted_sessions()+"\n\n"+recover_interrupted_sessions();
    if(id=="backup")return "Verified snapshot created:\n"+utf8(snapshot_folder(wide(first),second.empty()?"Game backup":second,cancel).wstring());
    if(id=="restore")return restore_snapshot(wide(first),wide(second),cancel);
    if(id=="memory-launch"){shell(system_exe(L"mdsched.exe").wstring());return "Windows Memory Diagnostic opened. No restart was requested by NovaTune.";}
    if(id=="diagnostic-export"){auto path=std::filesystem::path(wide(first));if(!path.is_absolute()||std::filesystem::exists(path)||unsafe_root(path.parent_path()))throw std::runtime_error("Choose a new .txt file in a personal folder");safe_components(path.parent_path());auto report=second.empty()?gamer_report(id,"","",cancel):second;if(report.size()>65536)throw std::runtime_error("Diagnostic report exceeds export limit");Handle output{CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};if(output.h==INVALID_HANDLE_VALUE)throw std::runtime_error(error());DWORD written=0;if(!WriteFile(output.h,report.data(),static_cast<DWORD>(report.size()),&written,nullptr)||written!=report.size()||!FlushFileBuffers(output.h))throw std::runtime_error("Report export failed");return "Privacy-preview report exported:\n"+first;}
    return gamer_report(id,first,second,cancel);
}
}
