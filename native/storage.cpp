#include "storage.hpp"
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
namespace hr {
namespace {
struct Handle {HANDLE h=nullptr;explicit Handle(HANDLE value):h(value){}~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
unsigned long long ticks(FILETIME t){return (static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
std::filesystem::path known(REFKNOWNFOLDERID id){
    PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&value)))return {};
    std::filesystem::path path(value);CoTaskMemFree(value);return path;
}
bool beneath(const std::filesystem::path& path,const std::filesystem::path& root){
    auto p=lower(utf8(path.lexically_normal().wstring())),r=lower(utf8(root.lexically_normal().wstring()));
    if(r.empty())return false;if(r.back()!='\\')r+='\\';return p.compare(0,r.size(),r)==0;
}
std::set<std::string> running_browsers(){
    std::set<std::string> out;Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    PROCESSENTRY32W entry{};entry.dwSize=sizeof entry;
    if(Process32FirstW(snapshot.h,&entry))do{out.insert(lower(utf8(entry.szExeFile)));}while(Process32NextW(snapshot.h,&entry));
    return out;
}
bool update_busy(){
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)return true;
    bool busy=false;
    for(auto name:{L"wuauserv",L"bits",L"TrustedInstaller"}){
        SC_HANDLE item=OpenServiceW(manager,name,SERVICE_QUERY_STATUS);if(!item){busy=true;continue;}
        SERVICE_STATUS status{};if(!QueryServiceStatus(item,&status) || status.dwCurrentState!=SERVICE_STOPPED)busy=true;
        CloseServiceHandle(item);
    }
    CloseServiceHandle(manager);return busy;
}
bool snapshot(const std::filesystem::path& path,StorageFile& out){
    Handle file(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if(file.h==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.h,&info)||info.nNumberOfLinks!=1||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    out.path=path;out.bytes=(static_cast<unsigned long long>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    out.modified=ticks(info.ftLastWriteTime);out.id=(static_cast<unsigned long long>(info.nFileIndexHigh)<<32)|info.nFileIndexLow;out.volume=info.dwVolumeSerialNumber;return true;
}
bool same(HANDLE file,const StorageFile& expected){
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file,&info))return false;
    return info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
        info.dwVolumeSerialNumber==expected.volume && ((static_cast<unsigned long long>(info.nFileIndexHigh)<<32)|info.nFileIndexLow)==expected.id &&
        ticks(info.ftLastWriteTime)==expected.modified && ((static_cast<unsigned long long>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)==expected.bytes;
}
std::string sha256(const StorageFile& file,std::atomic<bool>& cancel){
    Handle input(CreateFileW(file.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(input.h==INVALID_HANDLE_VALUE || !same(input.h,file))return {};
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 is unavailable");
    std::vector<BYTE> object;
    struct Close {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Close(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} close{algorithm,hash};
    DWORD size=0,returned=0;
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<BYTE*>(&size),sizeof size,&returned,0)<0)return {};
    object.resize(size);
    if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0)return {};
    std::vector<BYTE> block(1024*1024);DWORD bytes=0;
    while(!cancel){
        if(!ReadFile(input.h,block.data(),static_cast<DWORD>(block.size()),&bytes,nullptr))return {};
        if(!bytes)break;
        if(BCryptHashData(hash,block.data(),bytes,0)<0)return {};
    }
    std::array<BYTE,32> digest{};
    if(cancel || !same(input.h,file) || BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)return {};
    return hex(std::string(reinterpret_cast<char*>(digest.data()),digest.size()));
}
bool personal(const std::filesystem::path& path){
    const std::set<std::string> types={".zip",".7z",".rar",".tar",".gz",".iso",".pdf",".docx",".xlsx",".pptx",".txt",".png",".jpg",".jpeg",".webp",".gif",".mp4",".mkv",".mov",".mp3",".wav",".flac",".csv",".log"};
    return types.count(lower(utf8(path.extension().wstring())))>0;
}
bool protected_path(const std::filesystem::path& path){
    wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
    if(beneath(path,windows))return true;
    for(const auto& root:{known(FOLDERID_ProgramFiles),known(FOLDERID_ProgramFilesX86),known(FOLDERID_ProgramData),known(FOLDERID_LocalAppData),known(FOLDERID_RoamingAppData)})
        if(!root.empty() && beneath(path,root))return true;
    return false;
}
}
std::vector<CacheTarget> cache_targets(){
    std::vector<CacheTarget> out;auto local=known(FOLDERID_LocalAppData),roaming=known(FOLDERID_RoamingAppData);
    wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);std::filesystem::path system(windows);
    out.push_back({"user-temp","User temporary files","", "",local/L"Temp",24});
    out.push_back({"windows-temp","Windows temporary files","","Locked files are skipped.",system/L"Temp",24});
    out.push_back({"prefetch","Windows Prefetch","","Windows rebuilds these files; clearing them can slow startup.",system/L"Prefetch",168});
    out.push_back({"directx","DirectX shader cache","","Shaders rebuild; the next game launch can stutter.",local/L"D3DSCache",168});
    out.push_back({"nvidia-dx","NVIDIA DirectX shader cache","","Shaders rebuild on demand.",local/L"NVIDIA/DXCache",168});
    out.push_back({"amd-dx","AMD DirectX shader cache","","Shaders rebuild on demand.",local/L"AMD/DxCache",168});
    out.push_back({"update-download","Windows Update downloads","","Skipped while Update, BITS or Windows Modules Installer is running.",system/L"SoftwareDistribution/Download",168});
    struct Browser {const char* id;const char* name;const wchar_t* folder;const char* exe;};
    const Browser browsers[]={{"chrome","Chrome",L"Google/Chrome/User Data","chrome.exe"},{"edge","Edge",L"Microsoft/Edge/User Data","msedge.exe"},{"brave","Brave",L"BraveSoftware/Brave-Browser/User Data","brave.exe"}};
    for(const auto& b:browsers){
        auto root=local/b.folder;std::error_code ec;if(!std::filesystem::is_directory(root,ec))continue;
        for(std::filesystem::directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec)){
            if(!it->is_directory(ec))continue;auto leaf=it->path().filename().wstring();
            if(leaf!=L"Default" && leaf.compare(0,8,L"Profile ")!=0)continue;
            auto title=std::string(b.name)+" / "+utf8(leaf);
            for(auto sub:{L"Cache",L"Code Cache",L"GPUCache"})out.push_back({std::string(b.id)+"-cache",title+" / "+utf8(sub),b.exe,"Close this browser before cleaning.",it->path()/sub,0});
            for(auto file:{L"Network/Cookies",L"Network/Cookies-wal",L"Network/Cookies-shm"})out.push_back({std::string(b.id)+"-cookies",title+" / cookies",b.exe,"Signs you out. Saved passwords are not selected.",it->path()/file,0,true});
        }
        out.push_back({std::string(b.id)+"-logs",std::string(b.name)+" / crash logs",b.exe,"Close this browser before cleaning.",root/L"Crashpad/reports",24});
    }
    for(auto pair:{std::make_pair(local/L"Mozilla/Firefox/Profiles",false),std::make_pair(roaming/L"Mozilla/Firefox/Profiles",true)}){
        std::error_code ec;if(!std::filesystem::is_directory(pair.first,ec))continue;
        for(std::filesystem::directory_iterator it(pair.first,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec)){
            if(!it->is_directory(ec))continue;auto name="Firefox / "+utf8(it->path().filename().wstring());
            if(!pair.second)for(auto sub:{L"cache2",L"startupCache",L"shader-cache"})out.push_back({"firefox-cache",name+" / "+utf8(sub),"firefox.exe","Close Firefox before cleaning.",it->path()/sub,0});
            else for(auto file:{L"cookies.sqlite",L"cookies.sqlite-wal",L"cookies.sqlite-shm"})out.push_back({"firefox-cookies",name+" / cookies","firefox.exe","Signs you out. logins.json and key4.db are not selected.",it->path()/file,0,true});
        }
    }
    out.erase(std::remove_if(out.begin(),out.end(),[](const CacheTarget& target){std::error_code ec;return !std::filesystem::exists(target.path,ec);}),out.end());return out;
}
std::vector<StorageFile> scan_caches(const std::vector<CacheTarget>& selected,std::atomic<bool>& cancel){
    auto allowed=cache_targets();auto browsers=running_browsers();std::vector<StorageFile> result;
    FILETIME time;GetSystemTimeAsFileTime(&time);auto current=ticks(time);
    for(const auto& target:selected){
        if(cancel)break;
        if(!target.browser.empty() && browsers.count(target.browser))continue;
        if(target.id=="update-download" && update_busy())continue;
        if(std::none_of(allowed.begin(),allowed.end(),[&](const CacheTarget& known){return known.id==target.id && known.path==target.path;}))throw std::runtime_error("Unknown cleanup target");
        std::vector<std::filesystem::path> pending{target.path};
        while(!pending.empty()&&!cancel){
            auto path=pending.back();pending.pop_back();DWORD attr=GetFileAttributesW(path.c_str());
            if(attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_REPARSE_POINT))continue;
            if(attr&FILE_ATTRIBUTE_DIRECTORY){
                std::error_code ec;for(std::filesystem::directory_iterator it(path,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec))pending.push_back(it->path());
                continue;
            }
            StorageFile file;if(!snapshot(path,file) || file.modified>current || current-file.modified<static_cast<unsigned long long>(target.age_hours)*3600*10000000)continue;
            file.root=target.single?target.path.parent_path():target.path;file.category=target.id;file.browser=target.browser;file.age_hours=target.age_hours;result.push_back(std::move(file));
            if(result.size()>=100000)return result;
        }
    }
    return result;
}
CleanupResult clean_caches(const std::vector<StorageFile>& files){
    CleanupResult result;auto allowed=cache_targets();auto browsers=running_browsers();bool updates=update_busy();
    for(const auto& expected:files){
        bool valid=std::any_of(allowed.begin(),allowed.end(),[&](const CacheTarget& root){return root.id==expected.category && (root.single?root.path==expected.path:beneath(expected.path,root.path));});
        if(!valid || (!expected.browser.empty() && browsers.count(expected.browser)) || (expected.category=="update-download"&&updates)){++result.skipped;continue;}
        Handle file(CreateFileW(expected.path.c_str(),DELETE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(file.h==INVALID_HANDLE_VALUE||!same(file.h,expected)){++result.skipped;continue;}
        wchar_t final_path[32768]{};DWORD length=GetFinalPathNameByHandleW(file.h,final_path,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        std::wstring final=final_path;if(final.compare(0,4,L"\\\\?\\")==0)final.erase(0,4);
        if(!length || length>=32768 || !beneath(std::filesystem::path(final),expected.root)){++result.skipped;continue;}
        FILETIME time;GetSystemTimeAsFileTime(&time);
        if(expected.modified>ticks(time) || ticks(time)-expected.modified<static_cast<unsigned long long>(expected.age_hours)*3600*10000000){++result.skipped;continue;}
        FILE_DISPOSITION_INFO disposition{TRUE};
        if(SetFileInformationByHandle(file.h,FileDispositionInfo,&disposition,sizeof disposition)){++result.deleted;result.bytes+=expected.bytes;}else ++result.skipped;
    }
    return result;
}
StorageAnalysis analyze_storage(const std::filesystem::path& requested,bool duplicates,std::atomic<bool>& cancel){
    StorageAnalysis result;result.root=std::filesystem::weakly_canonical(requested);
    if(!std::filesystem::is_directory(result.root))throw std::runtime_error("Choose an existing folder");
    std::map<std::wstring,FolderUse> sizes;std::map<unsigned long long,std::vector<StorageFile>> candidates;
    std::vector<std::filesystem::path> pending{result.root};
    while(!pending.empty()&&!cancel){
        auto path=pending.back();pending.pop_back();DWORD attr=GetFileAttributesW(path.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_REPARSE_POINT))continue;
        if(attr&FILE_ATTRIBUTE_DIRECTORY){
            std::error_code ec;for(std::filesystem::directory_iterator it(path,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec))pending.push_back(it->path());continue;
        }
        StorageFile file;if(!snapshot(path,file))continue;file.root=result.root;
        result.bytes+=file.bytes;++result.files;
        auto relative=path.lexically_relative(result.root);auto first=relative.begin();
        auto top=first==relative.end()?path:result.root/(*first);auto& group=sizes[top.wstring()];group.path=top;group.bytes+=file.bytes;++group.files;
        result.largest.push_back(file);
        if(result.largest.size()>200){std::sort(result.largest.begin(),result.largest.end(),[](const StorageFile& a,const StorageFile& b){return a.bytes>b.bytes;});result.largest.resize(100);}
        if(duplicates && file.bytes && personal(path) && !protected_path(path))candidates[file.bytes].push_back(file);
        if(result.files>=500000){result.capped=true;break;}
    }
    for(auto& item:sizes)result.folders.push_back(std::move(item.second));
    std::sort(result.folders.begin(),result.folders.end(),[](const FolderUse& a,const FolderUse& b){return a.bytes>b.bytes;});
    std::sort(result.largest.begin(),result.largest.end(),[](const StorageFile& a,const StorageFile& b){return a.bytes>b.bytes;});if(result.largest.size()>100)result.largest.resize(100);
    if(duplicates)for(auto& same_size:candidates){
        if(cancel)break;if(same_size.second.size()<2)continue;
        std::map<std::string,std::vector<StorageFile>> groups;
        for(const auto& file:same_size.second){if(cancel)break;auto digest=sha256(file,cancel);if(!digest.empty())groups[digest].push_back(file);}
        for(auto& group:groups)if(group.second.size()>1)result.duplicates.push_back({std::move(group.second),group.first});
    }
    return result;
}
CleanupResult recycle_duplicates(HWND owner,const std::vector<std::pair<StorageFile,StorageFile>>& files){
    CleanupResult result;auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized))throw std::runtime_error("Recycle Bin COM initialization failed");
    struct Shutdown{~Shutdown(){CoUninitialize();}} shutdown;
    for(const auto& pair:files){
        const auto& file=pair.first;const auto& keeper=pair.second;
        if(file.path==keeper.path || !personal(file.path) || protected_path(file.path) || !beneath(file.path,file.root)){++result.skipped;continue;}
        std::atomic<bool> cancel{false};auto digest=sha256(file,cancel);
        if(digest.empty() || digest!=sha256(keeper,cancel)){++result.skipped;continue;}
        wchar_t volume[MAX_PATH]{};
        if(!GetVolumePathNameW(file.path.c_str(),volume,MAX_PATH)||GetDriveTypeW(volume)!=DRIVE_FIXED){++result.skipped;continue;}
        Handle keeper_lock(CreateFileW(keeper.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(keeper_lock.h==INVALID_HANDLE_VALUE||!same(keeper_lock.h,keeper)){++result.skipped;continue;}
        Handle lock(CreateFileW(file.path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(lock.h==INVALID_HANDLE_VALUE||!same(lock.h,file)){++result.skipped;continue;}
        IFileOperation* operation=nullptr;IShellItem* item=nullptr;
        auto status=CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_IFileOperation,reinterpret_cast<void**>(&operation));
        if(SUCCEEDED(status))status=SHCreateItemFromParsingName(file.path.c_str(),nullptr,IID_IShellItem,reinterpret_cast<void**>(&item));
        if(SUCCEEDED(status)){
            operation->SetOwnerWindow(owner);operation->SetOperationFlags(FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT|0x00080000);
            status=operation->DeleteItem(item,nullptr);if(SUCCEEDED(status))status=operation->PerformOperations();
            BOOL aborted=TRUE;operation->GetAnyOperationsAborted(&aborted);
            if(SUCCEEDED(status)&&!aborted){++result.deleted;result.bytes+=file.bytes;}else ++result.skipped;
        }else ++result.skipped;
        if(item)item->Release();if(operation)operation->Release();
    }
    return result;
}
void reveal(const std::filesystem::path& path){
    auto args=L"/select,"+quote(path.wstring());
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("Could not reveal this file");
}
}
