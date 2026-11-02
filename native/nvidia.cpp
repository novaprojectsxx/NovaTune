#include "nvidia.hpp"
#if defined(__MINGW32__) && !defined(__success)
#define __success(expression)
#endif
                                                                             
                                                              
#include "../vendor/nvapi/nvapi_lite_common.h"
#include "../vendor/nvapi/nvapi_lite_sli.h"
#include "../vendor/nvapi/nvapi_lite_surround.h"
#include "../vendor/nvapi/nvapi_lite_stereo.h"
#include "../vendor/nvapi/nvapi_lite_d3dext.h"
#include "../vendor/nvapi/nvapi.h"
#include "../vendor/nvapi/NvApiDriverSettings.h"
#include "../vendor/nvapi/nvapi_interface.h"
#include <algorithm>
#include <stdexcept>
namespace hr {
namespace {
class Driver {
    HMODULE module=nullptr;
    using Query=void*(__cdecl*)(unsigned);
    Query query=nullptr;
    decltype(&NvAPI_Initialize) initialize=nullptr;
    decltype(&NvAPI_Unload) unload=nullptr;
    decltype(&NvAPI_DRS_CreateSession) create=nullptr;
    decltype(&NvAPI_DRS_DestroySession) destroy=nullptr;
    decltype(&NvAPI_DRS_LoadSettings) load=nullptr;
    decltype(&NvAPI_DRS_SaveSettings) save=nullptr;
    decltype(&NvAPI_DRS_GetBaseProfile) base=nullptr;
    decltype(&NvAPI_DRS_GetSetting) get_setting=nullptr;
    decltype(&NvAPI_DRS_SetSetting) set_setting=nullptr;
    decltype(&NvAPI_DRS_DeleteProfileSetting) delete_setting=nullptr;
    decltype(&NvAPI_DRS_SaveSettingsToFile) export_settings=nullptr;
    NvDRSSessionHandle session=nullptr;NvDRSProfileHandle profile=nullptr;
    template<class T>T api(const char* name){
        for(const auto& entry:nvapi_interface_table)if(strcmp(entry.func,name)==0){auto pointer=reinterpret_cast<T>(query(entry.id));if(pointer)return pointer;break;}
        throw std::runtime_error(std::string("NVIDIA driver API unavailable: ")+name);
    }
    void ok(NvAPI_Status status,const char* action){if(status!=NVAPI_OK)throw std::runtime_error(std::string(action)+" failed (NVIDIA status "+std::to_string(status)+")");}
public:
    Driver(){
        module=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw std::runtime_error("Compatible NVIDIA driver API not installed");
        try{
            query=reinterpret_cast<Query>(GetProcAddress(module,"nvapi_QueryInterface"));if(!query)throw std::runtime_error("NVIDIA QueryInterface unavailable");
            initialize=api<decltype(initialize)>("NvAPI_Initialize");unload=api<decltype(unload)>("NvAPI_Unload");ok(initialize(),"Initialize NVIDIA API");
            create=api<decltype(create)>("NvAPI_DRS_CreateSession");destroy=api<decltype(destroy)>("NvAPI_DRS_DestroySession");
            load=api<decltype(load)>("NvAPI_DRS_LoadSettings");save=api<decltype(save)>("NvAPI_DRS_SaveSettings");
            base=api<decltype(base)>("NvAPI_DRS_GetBaseProfile");get_setting=api<decltype(get_setting)>("NvAPI_DRS_GetSetting");set_setting=api<decltype(set_setting)>("NvAPI_DRS_SetSetting");
            delete_setting=api<decltype(delete_setting)>("NvAPI_DRS_DeleteProfileSetting");export_settings=api<decltype(export_settings)>("NvAPI_DRS_SaveSettingsToFile");
            ok(create(&session),"Create settings session");ok(load(session),"Load settings");ok(base(session,&profile),"Get global profile");
        }catch(...){if(session&&destroy)destroy(session);if(unload)unload();FreeLibrary(module);module=nullptr;throw;}
    }
    ~Driver(){if(session)destroy(session);if(unload)unload();if(module)FreeLibrary(module);}
    NVDRS_SETTING current(){
        NVDRS_SETTING setting{};setting.version=NVDRS_SETTING_VER;ok(get_setting(session,profile,PS_SHADERDISKCACHE_MAX_SIZE_ID,&setting),"Read shader-cache size");
        if(setting.settingType!=NVDRS_DWORD_TYPE)throw std::runtime_error("Driver uses an unsupported shader-cache setting format");return setting;
    }
    void backup(const std::filesystem::path& path){
        std::filesystem::create_directories(path.parent_path());auto text=path.wstring();NvAPI_UnicodeString file{};
        if(text.size()>=NVAPI_UNICODE_STRING_MAX)throw std::runtime_error("NVIDIA backup path is too long");
        for(size_t i=0;i<text.size();++i)file[i]=static_cast<NvU16>(text[i]);
        ok(export_settings(session,file),"Export NVIDIA settings baseline");
    }
    void set(unsigned megabytes){
        auto setting=current();setting.u32CurrentValue=megabytes;ok(set_setting(session,profile,&setting),"Set shader-cache size");ok(save(session),"Save NVIDIA settings");
    }
    void restore(unsigned megabytes,bool predefined){
        if(predefined){ok(delete_setting(session,profile,PS_SHADERDISKCACHE_MAX_SIZE_ID),"Restore driver-managed cache size");ok(save(session),"Save NVIDIA defaults");}
        else set(megabytes);
    }
};
}
std::string nvidia_cache_info(){Driver driver;auto setting=driver.current();return "NVIDIA global shader-cache limit: "+std::to_string(setting.u32CurrentValue)+" MB. Driver-defined: "+(setting.isCurrentPredefined?"yes":"no");}
void nvidia_cache_size(Store& store,unsigned megabytes){
    if(megabytes!=10240 && megabytes!=102400)throw std::runtime_error("Choose a supported 10 GB or 100 GB preset");
    Driver driver;auto before=driver.current();if(before.u32CurrentValue==megabytes)return;
    auto backup=store.path.parent_path()/L"driver-backups"/(L"nvidia-"+std::to_wstring(GetTickCount64())+L".nvdrs");driver.backup(backup);
    auto value=std::to_string(before.u32CurrentValue)+":"+(before.isCurrentPredefined?"1":"0");
    store.rows.push_back({"journal","nv-cache","",value,std::to_string(megabytes),utf8(backup.wstring())});store.save();driver.set(megabytes);store.log("NVIDIA shader cache changed",std::to_string(megabytes)+" MB");
}
void undo_nvidia_cache(const Row& row){
    Driver driver;auto current=driver.current();auto colon=row[3].find(':');unsigned before=static_cast<unsigned>(std::stoul(row[3].substr(0,colon)));
    if(current.u32CurrentValue==before)return;if(current.u32CurrentValue!=std::stoul(row[4]))throw std::runtime_error("NVIDIA cache limit changed outside NovaTune");
    driver.restore(before,row[3].substr(colon+1)=="1");
}
}
