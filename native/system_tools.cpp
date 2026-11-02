#include "system_tools.hpp"
#include "nvidia.hpp"
#include "memory.hpp"
#include <powrprof.h>
#include <powersetting.h>
#include <shlobj.h>
#include <dxgi.h>
#include <regex>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
namespace hr {
namespace {
GUID guid(const wchar_t* text){GUID value{};if(FAILED(CLSIDFromString(text,&value)))throw std::runtime_error("Invalid power scheme");return value;}
std::wstring strguid(const GUID& value){wchar_t text[40]{};StringFromGUID2(value,text,40);return text;}
std::wstring literal(const std::string& text){auto value=wide(text);size_t i=0;while((i=value.find(L'\'',i))!=std::wstring::npos){value.insert(i,1,L'\'');i+=2;}return L"'"+value+L"'";}
std::filesystem::path local_data(){PWSTR text=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&text)))return {};std::filesystem::path root(text);CoTaskMemFree(text);return root;}
bool admx(const wchar_t* file,const std::string& policy){
    wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
    auto text=read_output(std::filesystem::path(windows)/L"PolicyDefinitions"/file);return text.find(policy)!=std::string::npos;
}
bool hardware_gpu(){
    IDXGIFactory1* factory=nullptr;IDXGIAdapter1* adapter=nullptr;bool result=false;
    if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)))){
        if(factory->EnumAdapters1(0,&adapter)==S_OK){DXGI_ADAPTER_DESC1 info{};if(SUCCEEDED(adapter->GetDesc1(&info)))result=!(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&info.VendorId!=0x1414;adapter->Release();}
        factory->Release();
    }
    return result;
}
struct Policy {std::string id;HKEY hive;const wchar_t* path;const wchar_t* name;DWORD value;};
const Policy policies[]={
    {"recall-snapshots-off",HKEY_CURRENT_USER,L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsAI",L"DisableAIDataAnalysis",1},
    {"clicktodo-off",HKEY_CURRENT_USER,L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsAI",L"DisableClickToDo",1},
    {"paint-cocreator-off",HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Paint",L"DisableCocreator",1},
    {"paint-imagecreator-off",HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Paint",L"DisableImageCreator",1},
    {"paint-generative-off",HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Paint",L"DisableGenerativeFill",1},
    {"dvr-off",HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR",L"AllowGameDVR",0},
    {"gamebar-hotkey-off",HKEY_CURRENT_USER,L"Software\\Microsoft\\GameBar",L"UseNexusForGameBarEnabled",0},
    {"hags-on",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",L"HwSchMode",2},
    {"hags-off",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",L"HwSchMode",1},
    {"mpo-off",HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\Dwm",L"OverlayTestMode",5},
    {"mpo-default",HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\Dwm",L"OverlayTestMode",0},
    {"search-hide",HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Search",L"SearchboxTaskbarMode",0},
    {"search-web-off",HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search",L"DisableWebSearch",1},
    {"hvci-off",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",L"Enabled",0},
    {"hvci-on",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",L"Enabled",1},
    {"vbs-off",HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",L"EnableVirtualizationBasedSecurity",0}
};
std::string unavailable(const std::string& id){
    if(id=="recall-snapshots-off"&&!admx(L"WindowsCopilot.admx","DisableAIDataAnalysis"))return "This build does not expose the Recall policy.";
    if(id=="clicktodo-off"&&!admx(L"WindowsCopilot.admx","DisableClickToDo"))return "The Click to Do policy is not exposed by this Windows build.";
    if(id.find("paint-")==0){
        auto name=id=="paint-cocreator-off"?"DisableCocreator":id=="paint-imagecreator-off"?"DisableImageCreator":"DisableGenerativeFill";
        if(!admx(L"WindowsCopilot.admx",name))return "This Paint AI policy is not exposed by this Windows build.";
    }
    if((id=="hags-on"||id=="hags-off")&&!hardware_gpu())return "No supported hardware adapter was detected. Registry changes cannot create HAGS support.";
    if(id=="search-web-off"&&!admx(L"Search.admx","DisableWebSearch"))return "This Windows edition/build does not expose the web-search policy.";
    if(id=="hvci-off"||id=="hvci-on"||id=="vbs-off"){
        for(const auto& path:{L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity"}){
            auto value=registry_value(HKEY_LOCAL_MACHINE,path,L"Locked");
            if(value!="-"&&value!="k" && value!="4:00000000")return "The setting is UEFI/policy locked. Use Windows Security or your administrator; NovaTune will not bypass it.";
        }
    }
    return {};
}
std::vector<std::string> ids(const std::string& input){
    std::istringstream stream(input);std::vector<std::string> result;std::string value;
    while(std::getline(stream,value,',')){
        value=std::regex_replace(value,std::regex("^\\s+|\\s+$"),"");
        if(!std::regex_match(value,std::regex("[A-Za-z0-9][A-Za-z0-9.+_-]{1,150}")))throw std::runtime_error("Use exact WinGet package IDs separated by commas");
        result.push_back(value);
    }
    if(result.empty()||result.size()>30)throw std::runtime_error("Select between one and 30 package IDs");return result;
}
void parking(Store& store){
    auto scheme=guid(wide(active_plan()).c_str());
    auto subgroup=guid(L"{54533251-82be-4824-96c1-47b60b740d00}"),setting=guid(L"{0cc5b647-c1df-4637-891a-dec35c318583}");
    DWORD before=0;check(PowerReadACValueIndex(nullptr,&scheme,&subgroup,&setting,&before),"Read core-parking minimum");
    if(before==100)return;
    registry_backup(store,HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes");
    store.rows.push_back({"journal","power-setting",utf8(strguid(scheme)),std::to_string(before),"100","parking-ac"});store.save();
    check(PowerWriteACValueIndex(nullptr,&scheme,&subgroup,&setting,100),"Set core-parking minimum");
    check(PowerSetActiveScheme(nullptr,&scheme),"Apply power settings");store.log("Core parking changed","Active profile AC minimum set to 100%; battery settings retained");
}
void power_profile(Store& store,bool ultimate){
    auto template_id=guid(ultimate?L"{e9a42b02-d5df-448d-aa00-03f14749eb61}":L"{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}");
    GUID* created=nullptr;auto previous=active_plan();
    registry_backup(store,HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes");
    check(PowerDuplicateScheme(nullptr,&template_id,&created),"Create performance power plan");
    auto next=utf8(strguid(*created));LocalFree(created);
    store.rows.push_back({"journal","plan-created",next,previous,next,""});store.save();set_plan(next);store.log("Power plan created",ultimate?"Ultimate Performance":"High Performance");
}
}
std::string powershell_text(const std::wstring& script,DWORD timeout){
    auto result=command_output(system_exe(L"WindowsPowerShell/v1.0/powershell.exe").wstring(),{L"-NoProfile",L"-NonInteractive",L"-Command",L"$ErrorActionPreference='Stop'; "+script},{},timeout);
    if(result.code)throw std::runtime_error(result.output);return result.output;
}
std::vector<SystemTool> system_tools(){
    return {
        {"component-analyze","Repair","Analyze component store","Run native DISM analysis. WinSxS is never deleted manually.",false},
        {"component-clean","Repair","Clean superseded components","DISM StartComponentCleanup; does not use ResetBase. Removed superseded data cannot be restored by NovaTune.",true},
        {"repair","Repair","Repair Windows image and system files","DISM RestoreHealth, then SFC /scannow. Can take a long time; commands are not force-terminated.",true},
        {"sfc","Repair","Scan and repair protected system files","Run SFC /scannow and preserve the complete command output.",true},
        {"recall-info","AI","Inspect Recall optional feature","Read feature availability/state with DISM; does not assume Recall exists.",false},
        {"recall-remove","AI","Remove Recall optional feature","Disable/remove the Recall component. Existing Recall snapshots are deleted. Reboot may be required; snapshots cannot be restored.",true,true},
        {"recall-snapshots-off","AI","Disable Recall snapshots policy","Where supported. Existing snapshots may be deleted by Windows; reverting the policy does not recover them.",true,true},
        {"clicktodo-off","AI","Disable Click to Do policy","Only when the policy is present on this Windows build. Does not remove core AI frameworks.",true,true},
        {"paint-cocreator-off","AI","Disable Paint Cocreator","Apply the documented Paint AI policy where available.",true},
        {"paint-imagecreator-off","AI","Disable Paint Image Creator","Apply the documented Paint AI policy where available.",true},
        {"paint-generative-off","AI","Disable Paint Generative Fill","Apply the documented Paint AI policy where available.",true},
        {"dvr-off","Gaming","Disable Game DVR policy","Disable Windows game recording policy. NovaTune's own recorder is independent.",true,true},
        {"gamebar-hotkey-off","Gaming","Disable controller Game Bar shortcut","Disable the Xbox/controller entry shortcut. Removing Game Bar is a separate optional-app choice.",true},
        {"hags-on","GPU","Request HAGS enabled","Requires compatible Windows, WDDM driver and GPU. Requested registry mode is not proof of active hardware scheduling. Reboot.",true,true},
        {"hags-off","GPU","Request HAGS disabled","A driver-specific troubleshooting option. Benchmark before/after; gains are not guaranteed.",true,true},
        {"mpo-off","GPU","Disable MPO (advanced workaround)","Diagnostic OverlayTestMode setting, not a universal FPS optimization. Can affect HDR, presentation and power use.",true,true},
        {"mpo-default","GPU","Request normal MPO behavior","Restore the diagnostic mode to zero. Use Revert to recover the exact prior value or absence.",true,true},
        {"graphics-settings","GPU","Open Windows graphics controls","Native Windows controls for windowed-game optimizations, HAGS and per-app graphics preference.",false},
        {"nv-cache-10","GPU","NVIDIA shader cache: 10 GB","Uses the public NVIDIA driver-settings API and exports a complete profile baseline first. Requires a compatible NVIDIA driver.",true},
        {"nv-cache-100","GPU","NVIDIA shader cache: 100 GB","Uses the public NVIDIA driver-settings API. Allow up to 100 GB disk usage. Existing driver setting is backed up and reversible.",true},
        {"nvidia-settings","GPU","Open NVIDIA shader-cache controls","Vendor control panel owns supported cache-size choices. NovaTune does not invent an AMD/DirectX universal size key.",false},
        {"ultimate","Power","Create Ultimate Performance plan","Duplicate and activate the Windows performance template. May increase heat/power; does not guarantee pinned clocks or higher FPS.",true},
        {"high-performance","Power","Create High Performance plan","Duplicate and activate the high-performance template; previous active plan is journaled.",true},
        {"standby-clear","Power","Release standby cache (advanced)","Explicit profiling-privilege operation. Cached pages rebuild; clearing them can increase stutter and is not a guaranteed FPS gain.",true},
        {"standby-agent-on","Power","Install pressure-only standby agent","Installs the compatible standby service under Program Files. Checks once a minute and releases cache only below 512 MB available, at most every five minutes. This is optional and can worsen performance.",true},
        {"standby-agent-off","Power","Remove NovaTune standby agent","Stops/removes only the compatible standby service. Does not alter Windows memory services.",true},
        {"parking-off","Power","Disable core parking on AC profile","Set the active profile's minimum parked-core percentage to 100%. AC only; exact previous value backed up.",true},
        {"dns","Network","Flush DNS resolver cache","Clears cached name lookups. Does not combine connections or guarantee lower in-game ping.",true},
        {"net-info","Network","Inspect network configuration","Read adapter and TCP configuration without changing it.",false},
        {"tcp-reset","Network","Reset TCP/IP configuration","Runs netsh int ip reset. Can disconnect networking and affect custom IP/VPN configuration. Reboot required.",true,true},
        {"winsock","Network","Reset Winsock catalog","Runs netsh winsock reset. Can affect network filters/VPNs; reboot required.",true,true},
        {"update-repair","Repair","Repair Windows Update download queue","Stop Update/BITS when possible, preserve the Download directory under a dated backup name, and restore previous service running state. No forced deletion.",true,true},
        {"update-settings","Repair","Open Windows Update pause controls","Windows does not provide a universal security-only consumer-update toggle. Use supported pause options for this build.",false},
        {"classic-menu","Shell","Request classic context menu","User-scope compatibility override. Windows builds may ignore it; Explorer restart/sign-out may be needed. Baseline is preserved.",true,true},
        {"search-hide","Shell","Hide taskbar search box","Reduce taskbar search clutter for this user.",true},
        {"search-web-off","Shell","Disable web-search policy","Only when the policy is available; edition/build support varies.",true,true},
        {"hvci-off","Security","Disable Memory Integrity (HVCI)","SECURITY TRADEOFF: reduces kernel code-integrity protection. Not a guaranteed FPS gain. Refuses locked configurations; reboot required.",true,true,true},
        {"hvci-on","Security","Enable Memory Integrity (HVCI)","Request kernel code-integrity protection. Hardware, drivers and Windows decide actual running state after reboot.",true,true,true},
        {"vbs-off","Security","Disable configured VBS startup","SECURITY TRADEOFF: may reduce virtualization-based protections. Does not disable Defender or Secure Boot. Refuses locked settings; reboot required.",true,true,true},
        {"security-settings","Security","Open Core Isolation controls","Inspect the actual Windows Security state and managed restrictions.",false}
    };
}
std::string inspect_tool(const std::string& id){
    auto catalogue=system_tools();if(std::none_of(catalogue.begin(),catalogue.end(),[&](const SystemTool& tool){return tool.id==id;}))throw std::invalid_argument("Unknown system tool");
    try{
        if(id=="nv-cache-10"||id=="nv-cache-100")return nvidia_cache_info();
        auto reason=unavailable(id);if(!reason.empty())return "Unavailable: "+reason;
        const wchar_t* command=nullptr;if(id=="component-analyze"||id=="component-clean"||id=="recall-info"||id=="recall-remove"||id=="repair")command=L"dism.exe";else if(id=="sfc")command=L"sfc.exe";else if(id=="dns"||id=="net-info")command=L"ipconfig.exe";else if(id=="tcp-reset"||id=="winsock")command=L"netsh.exe";
        if(command&&!std::filesystem::is_regular_file(system_exe(command)))return "Unavailable: the required Windows command is missing.";
        for(const auto& p:policies)if(p.id==id)return "Saved/requested registry state: "+registry_value(p.hive,p.path,p.name)+"\nActual effective state can depend on Windows policy, hardware and reboot.";
        if(id=="ultimate"||id=="high-performance"||id=="parking-off")return "Active power plan: "+active_plan();
        return "Available command. Review its description before applying.";
    }catch(const std::exception& e){return std::string("Unavailable: ")+e.what();}
}
CommandResult run_system_tool(Store& store,const std::string& id,const std::filesystem::path& log){
    if(id=="nv-cache-10"||id=="nv-cache-100"){nvidia_cache_size(store,id=="nv-cache-10"?10240:102400);return {0,nvidia_cache_info(),log};}
    auto reason=unavailable(id);if(!reason.empty())throw std::runtime_error(reason);
    for(const auto& p:policies)if(p.id==id){registry_dword(store,p.hive,p.path,p.name,p.value,id);return {0,"Setting backed up and requested. Reboot/sign-out may be required.",log};}
    if(id=="standby-clear"){release_standby();return {0,"Standby cache release requested. Allocations are not freed; cached pages rebuild.",log};}
    if(id=="standby-agent-on"||id=="standby-agent-off"){memory_service(store,id=="standby-agent-on");return {0,"NovaTune memory-agent configuration updated.",log};}
    if(id=="classic-menu"){
        std::wstring value;registry_change(store,HKEY_CURRENT_USER,L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32",L"",REG_SZ,std::string(2,'\0'),"Classic context menu");return {0,"Compatibility override backed up and applied. Explorer restart/sign-out may be needed.",log};
    }
    if(id=="ultimate"||id=="high-performance"){power_profile(store,id=="ultimate");return {0,"Performance plan created and activated; previous scheme saved in Undo.",log};}
    if(id=="parking-off"){parking(store);return {0,"AC core-parking setting updated; previous value saved in Undo.",log};}
    if(id=="graphics-settings"||id=="update-settings"||id=="security-settings"){
        shell(id=="graphics-settings"?L"ms-settings:display-advancedgraphics":id=="update-settings"?L"ms-settings:windowsupdate-options":L"windowsdefender://coreisolation");return {0,"Windows settings opened.",log};
    }
    if(id=="nvidia-settings"){
        auto path=std::filesystem::path(L"C:\\Program Files\\NVIDIA Corporation\\Control Panel Client\\nvcplui.exe");
        if(std::filesystem::exists(path))shell(path.wstring());else shell(L"shell:AppsFolder\\NVIDIACorp.NVIDIAControlPanel_56jybvy8sckqj!NVIDIACorp.NVIDIAControlPanel");
        return {0,"NVIDIA controls requested. Choose a driver-supported shader-cache size there.",log};
    }
    if(id=="component-analyze"||id=="component-clean"||id=="recall-info"||id=="recall-remove"){
        std::vector<std::wstring> args={L"/Online",L"/English"};
        if(id=="component-analyze")args.insert(args.end(),{L"/Cleanup-Image",L"/AnalyzeComponentStore"});
        if(id=="component-clean")args.insert(args.end(),{L"/Cleanup-Image",L"/StartComponentCleanup"});
        if(id=="recall-info")args.insert(args.end(),{L"/Get-FeatureInfo",L"/FeatureName:Recall"});
        if(id=="recall-remove")args.insert(args.end(),{L"/Disable-Feature",L"/FeatureName:Recall",L"/Remove",L"/NoRestart"});
        return command_output(system_exe(L"dism.exe").wstring(),args,log,7200000);
    }
    if(id=="repair"){
        auto image=command_output(system_exe(L"dism.exe").wstring(),{L"/Online",L"/Cleanup-Image",L"/RestoreHealth",L"/English"},log,7200000);
        if(image.code && image.code!=3010)return image;auto sfc_log=log;sfc_log+=L".sfc.log";
        auto sfc=command_output(system_exe(L"sfc.exe").wstring(),{L"/scannow"},sfc_log,7200000);sfc.output=image.output+"\n--- SFC ---\n"+sfc.output;return sfc;
    }
    if(id=="sfc")return command_output(system_exe(L"sfc.exe").wstring(),{L"/scannow"},log,7200000);
    if(id=="dns")return command_output(system_exe(L"ipconfig.exe").wstring(),{L"/flushdns"},log);
    if(id=="net-info")return command_output(system_exe(L"ipconfig.exe").wstring(),{L"/all"},log);
    if(id=="tcp-reset"){
        auto reset_log=log;reset_log+=L".reset.txt";return command_output(system_exe(L"netsh.exe").wstring(),{L"int",L"ip",L"reset",reset_log.wstring()},log);
    }
    if(id=="winsock")return command_output(system_exe(L"netsh.exe").wstring(),{L"winsock",L"reset"},log);
    if(id=="update-repair"){
        auto text=powershell_text(L"$names=@('bits','wuauserv'); $before=@{}; try { foreach($n in $names){$s=Get-Service -Name $n; $before[$n]=$s.Status.ToString(); if($s.Status -ne 'Stopped'){Stop-Service -Name $n -ErrorAction Stop; (Get-Service $n).WaitForStatus('Stopped',[TimeSpan]::FromSeconds(20))}}; $src=Join-Path $env:windir 'SoftwareDistribution\\Download'; if(Test-Path -LiteralPath $src){$backup=$src+'.NovaTune-'+(Get-Date -Format 'yyyyMMdd-HHmmss'); Rename-Item -LiteralPath $src -NewName (Split-Path -Leaf $backup) -ErrorAction Stop; 'Preserved update downloads: '+$backup}; 'Queue reset completed; no update files were permanently deleted.' } finally { foreach($n in $names){if($before[$n] -eq 'Running'){Start-Service -Name $n -ErrorAction Continue}} }",180000);
        atomic_write(log,text);return {0,text,log};
    }
    throw std::runtime_error("Unknown system tool");
}
std::filesystem::path winget_path(){
    auto path=local_data()/L"Microsoft/WindowsApps/winget.exe";if(!std::filesystem::exists(path))throw std::runtime_error("WinGet is unavailable. Install/update Microsoft App Installer through Microsoft Store.");return path;
}
CommandResult winget(const std::string& action,const std::string& query,const std::filesystem::path& log){
    auto exe=winget_path();
    if(action=="search")return command_output(exe.wstring(),{L"search",L"--query",wide(query),L"--source",L"winget",L"--accept-source-agreements",L"--disable-interactivity"},log);
    if(action=="updates")return command_output(exe.wstring(),{L"upgrade",L"--accept-source-agreements",L"--disable-interactivity"},log);
    if(action!="install"&&action!="upgrade"&&action!="uninstall")throw std::runtime_error("Unsupported package action");
    CommandResult all{0,"",log};
    for(const auto& id:ids(query)){
        auto item=log;item+=L"."+wide(id)+L".log";
        std::vector<std::wstring> args={wide(action),L"--id",wide(id),L"--exact",L"--silent",L"--disable-interactivity",L"--accept-source-agreements"};
        if(action!="uninstall")args.push_back(L"--accept-package-agreements");
        auto result=command_output(exe.wstring(),args,item,7200000);all.output+="--- "+id+" ---\n"+result.output+"\n";if(result.code)all.code=result.code;
    }
    atomic_write(log,all.output);return all;
}
void schedule_winget(Store& store,const std::string& requested,bool enable){
    if(!enable){powershell_text(L"Unregister-ScheduledTask -TaskName 'Headroom App Updates' -Confirm:$false -ErrorAction SilentlyContinue");store.set("winget.scheduled","");store.log("App updater disabled","NovaTune app updates");return;}
    auto packages=ids(requested);std::string joined;for(const auto& p:packages){if(!joined.empty())joined+=",";joined+=p;}
    auto executable=maintenance_exe();
    auto script=L"$a=New-ScheduledTaskAction -Execute "+literal(utf8(executable.wstring()))+L" -Argument "+literal("--winget-scheduled "+joined)+L"; $t=New-ScheduledTaskTrigger -Daily -At '12:00'; $p=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Highest; $s=New-ScheduledTaskSettingsSet -StartWhenAvailable -RunOnlyIfNetworkAvailable; Register-ScheduledTask -TaskName 'Headroom App Updates' -Action $a -Trigger $t -Principal $p -Settings $s -Description 'Only the explicitly selected package IDs are upgraded silently while this user is logged on.' -Force | Out-Null";
    powershell_text(script);store.set("winget.scheduled",joined);store.log("App updater enabled",joined);
}
void undo_power_setting(const Row& row){
    if(row[1]=="plan-created"){
        auto current=active_plan();if(lower(current)!=lower(row[4]) && lower(current)!=lower(row[3]))throw std::runtime_error("Power plan changed outside NovaTune");
        if(lower(current)==lower(row[4]))set_plan(row[3]);
        auto scheme=guid(wide(row[2]).c_str());check(PowerDeleteScheme(nullptr,&scheme),"Remove created power plan");return;
    }
    auto scheme=guid(wide(row[2]).c_str()),subgroup=guid(L"{54533251-82be-4824-96c1-47b60b740d00}"),setting=guid(L"{0cc5b647-c1df-4637-891a-dec35c318583}");
    DWORD current=0;check(PowerReadACValueIndex(nullptr,&scheme,&subgroup,&setting,&current),"Read core-parking state");
    if(current==std::stoul(row[3]))return;if(current!=std::stoul(row[4]))throw std::runtime_error("Power setting changed outside NovaTune");
    check(PowerWriteACValueIndex(nullptr,&scheme,&subgroup,&setting,static_cast<DWORD>(std::stoul(row[3]))),"Restore core parking");if(lower(active_plan())==lower(row[2]))PowerSetActiveScheme(nullptr,&scheme);
}
}
