#include "autoruns.hpp"
#include "system_tools.hpp"
#include <shlobj.h>
#include <wincrypt.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
namespace hr {
namespace {
std::wstring literal(const std::string& text){auto s=wide(text);size_t i=0;while((i=s.find(L'\'',i))!=std::wstring::npos){s.insert(i,1,L'\'');i+=2;}return L"'"+s+L"'";}
std::string decode64(const std::string& text){DWORD bytes=0;if(!CryptStringToBinaryA(text.c_str(),0,CRYPT_STRING_BASE64,nullptr,&bytes,nullptr,nullptr))return {};std::string out(bytes,'\0');CryptStringToBinaryA(text.c_str(),0,CRYPT_STRING_BASE64,reinterpret_cast<BYTE*>(&out[0]),&bytes,nullptr,nullptr);out.resize(bytes);return out;}
bool protected_command(const std::string& command){
    auto text=lower(command);
    for(auto word:{"\\windows\\","%windir%","%systemroot%","defender","securityhealth","antivirus","sophos","crowdstrike","sentinel","kaspersky","bitdefender","lsass","winlogon","headroom"})if(text.find(word)!=std::string::npos)return true;
    return command.empty();
}
std::filesystem::path folder(bool machine){
    PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(machine?FOLDERID_CommonStartup:FOLDERID_Startup,0,nullptr,&value)))return {};
    std::filesystem::path path(value);CoTaskMemFree(value);return path;
}
std::string task_identity(const Autorun& entry){return entry.target+"|"+entry.name;}
void set_task(const Autorun& entry,bool enabled){
    auto script=L"$t=Get-ScheduledTask -TaskPath "+literal(entry.target)+L" -TaskName "+literal(entry.name)+L" -ErrorAction Stop; ";
    script+=enabled?L"$t | Enable-ScheduledTask -ErrorAction Stop | Out-Null":L"$t | Disable-ScheduledTask -ErrorAction Stop | Out-Null";
    powershell_text(script);
}
Autorun fresh_task(const Autorun& expected){
    auto all=autoruns();auto found=std::find_if(all.begin(),all.end(),[&](const Autorun& entry){return entry.kind=="Task"&&task_identity(entry)==task_identity(expected);});
    if(found==all.end()||found->command!=expected.command)throw std::runtime_error("Task changed; refresh before changing it");return *found;
}
void apply(const Autorun& entry,bool enable,const std::string& backup){
    if(entry.kind=="Task"){
        auto current=fresh_task(entry);if(enable){if(current.enabled)return;}else if(!current.enabled)return;
        set_task(entry,enable);return;
    }
    if(entry.kind=="Folder"){
        auto source=std::filesystem::path(wide(enable?backup:entry.target)),destination=std::filesystem::path(wide(enable?entry.target:backup));
        if(!std::filesystem::is_regular_file(source))throw std::runtime_error("Startup file changed or is missing");
        if(std::filesystem::exists(destination))throw std::runtime_error("Destination already exists; no file overwritten");
        std::filesystem::create_directories(destination.parent_path());
        if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_COPY_ALLOWED|MOVEFILE_WRITE_THROUGH))throw std::runtime_error(error());return;
    }
    if(entry.kind=="Run"){
        auto split=entry.extra.find(':');DWORD type=static_cast<DWORD>(std::stoul(entry.extra.substr(0,split)));auto bytes=unhex(entry.extra.substr(split+1));
        HKEY key=nullptr;auto hive=entry.scope=="Machine"?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER;
        check(RegCreateKeyExW(hive,wide(entry.target).c_str(),0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr),"Open startup key");
        DWORD n=0,current_type=0;auto status=RegQueryValueExW(key,wide(entry.name).c_str(),nullptr,&current_type,nullptr,&n);
        if(enable){
            if(status!=ERROR_FILE_NOT_FOUND){RegCloseKey(key);throw std::runtime_error("Startup value already exists; not overwritten");}
            status=RegSetValueExW(key,wide(entry.name).c_str(),0,type,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()));
        }else{
            std::string current(n,'\0');
            if(status==ERROR_SUCCESS)status=RegQueryValueExW(key,wide(entry.name).c_str(),nullptr,&current_type,n?reinterpret_cast<BYTE*>(&current[0]):nullptr,&n);
            if(status!=ERROR_SUCCESS||current!=bytes||current_type!=type){RegCloseKey(key);throw std::runtime_error("Startup value changed; refresh first");}
            status=RegDeleteValueW(key,wide(entry.name).c_str());
        }
        RegCloseKey(key);check(status,"Change startup value");
    }
}
}
Row autorun_record(const Autorun& e){return {e.kind,e.name,e.scope,e.command,e.target,e.extra,e.risk,e.enabled?"1":"0",e.editable?"1":"0"};}
Autorun autorun_from_record(const Row& r){if(r.size()!=9)throw std::runtime_error("Invalid startup baseline");return {r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7]=="1",r[8]=="1"};}
std::vector<Autorun> autoruns(){
    std::vector<Autorun> result;
    for(const auto& s:startup()){
        if(s.type!=REG_SZ&&s.type!=REG_EXPAND_SZ)continue;
        std::wstring text(s.bytes.size()/2,L'\0');if(!text.empty())memcpy(&text[0],s.bytes.data(),text.size()*2);while(!text.empty()&&text.back()==0)text.pop_back();
        auto command=utf8(text);bool editable=!protected_command(command);
        result.push_back({"Run",s.name,s.scope,command,"Software\\Microsoft\\Windows\\CurrentVersion\\Run",std::to_string(s.type)+":"+hex(s.bytes),editable?"Review / unverified":"Protected OS/security",true,editable});
    }
    for(bool machine:{false,true}){
        auto path=folder(machine);if(path.empty())continue;std::error_code ec;
        for(std::filesystem::directory_iterator it(path,std::filesystem::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec)){
            if(!it->is_regular_file(ec)||(GetFileAttributesW(it->path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT))continue;
            auto name=utf8(it->path().filename().wstring());bool editable=!protected_command(name);
            result.push_back({"Folder",name,machine?"Machine":"User",utf8(it->path().wstring()),utf8(it->path().wstring()),"",editable?"Review / unverified":"Protected OS/security",true,editable});
        }
    }
    try{
        auto output=powershell_text(L"Get-ScheduledTask | ForEach-Object { $a=($_.Actions | ForEach-Object { $_.Execute+' '+$_.Arguments }) -join '; '; $fields=@($_.TaskName,$_.TaskPath,$_.State.ToString(),$a,$_.Author); ($fields | ForEach-Object { [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes([string]$_)) }) -join [char]9 }",90000);
        std::istringstream input(output);std::string line;
        while(std::getline(input,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
            std::istringstream fields(line);std::string item;Row row;while(std::getline(fields,item,'\t'))row.push_back(decode64(item));if(!line.empty()&&line.back()=='\t')row.push_back("");if(row.size()!=5)continue;
            bool editable=lower(row[1]).find("\\microsoft\\")!=0 && !protected_command(row[3]) && lower(row[4]).find("microsoft")==std::string::npos;
            result.push_back({"Task",row[0],"Scheduled task",row[3],row[1],row[4],editable?"Review third-party task":"Protected OS/security task",row[2]!="Disabled",editable});
        }
    }catch(const std::exception& e){result.push_back({"Info","Scheduled tasks unavailable","",e.what(),"","","Unknown",false,false});}
    std::sort(result.begin(),result.end(),[](const Autorun& a,const Autorun& b){return lower(a.name)<lower(b.name);});return result;
}
void disable_autorun(Store& store,const Autorun& entry){
    if(!entry.editable||!entry.enabled)throw std::runtime_error("This entry is protected or already disabled");
    auto encoded=encode({autorun_record(entry)});std::string backup;
    if(entry.kind=="Run")registry_backup(store,entry.scope=="Machine"?HKEY_LOCAL_MACHINE:HKEY_CURRENT_USER,wide(entry.target));
    else{
        auto root=store.path.parent_path()/L"startup-backups"/std::to_wstring(GetTickCount64());std::filesystem::create_directories(root);
        if(entry.kind=="Folder")backup=utf8((root/std::filesystem::path(wide(entry.target)).filename()).wstring());
        if(entry.kind=="Task"){
            auto current=fresh_task(entry);if(!current.editable||!current.enabled)throw std::runtime_error("Task changed; refresh first");
            auto xml=powershell_text(L"Export-ScheduledTask -TaskPath "+literal(entry.target)+L" -TaskName "+literal(entry.name));
            auto path=root/L"task.xml";atomic_write(path,xml);backup=utf8(path.wstring());
        }
    }
    store.rows.push_back({"journal","autorun",encoded,"1","0",backup});store.save();
    apply(entry,false,backup);store.rows.push_back({"disabled-autorun",encoded,backup});store.log("Startup entry disabled",entry.name);
}
void restore_autorun(Store& store,const Row& record){
    auto decoded=decode(record[1]);if(decoded.size()!=1)throw std::runtime_error("Invalid startup backup");auto entry=autorun_from_record(decoded[0]);apply(entry,true,record[2]);
    store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const Row& r){return (r[0]=="disabled-autorun"&&r[1]==record[1])||(r[0]=="journal"&&r[1]=="autorun"&&r[2]==record[1]);}),store.rows.end());
    store.log("Startup entry restored",entry.name);
}
void undo_autorun(const Row& journal){
    auto decoded=decode(journal[2]);if(decoded.size()!=1)throw std::runtime_error("Invalid startup undo data");auto entry=autorun_from_record(decoded[0]);
    if(entry.kind=="Run"){
        auto all=startup();auto found=std::find_if(all.begin(),all.end(),[&](const Startup& s){return s.name==entry.name&&s.scope==entry.scope;});
        if(found!=all.end()){
            auto split=entry.extra.find(':');
            if(found->type==std::stoul(entry.extra.substr(0,split))&&found->bytes==unhex(entry.extra.substr(split+1)))return;
            throw std::runtime_error("Startup entry changed outside NovaTune");
        }
    }
    if(entry.kind=="Folder"&&std::filesystem::exists(std::filesystem::path(wide(entry.target)))&&!std::filesystem::exists(std::filesystem::path(wide(journal[5]))))return;
    apply(entry,true,journal[5]);
}
}
