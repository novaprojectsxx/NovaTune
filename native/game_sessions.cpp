#include "game_sessions.hpp"
#include "registry.hpp"
#include "profiles.hpp"
#include <sddl.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>
#include <stdexcept>
namespace hr {
namespace {
struct Handle {HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Shared {volatile LONG result;};
unsigned long long ticks(FILETIME value){return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
std::string executable(DWORD pid){Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};wchar_t path[32768]{};DWORD n=32768;if(!process.h||!QueryFullProcessImageNameW(process.h,0,path,&n))return {};return utf8(path);}
unsigned long long birth(HANDLE process){FILETIME c{},e{},k{},u{};return GetProcessTimes(process,&c,&e,&k,&u)?ticks(c):0;}
std::filesystem::path guard_root(){auto path=data_dir()/L"session-guards";std::filesystem::create_directories(path);return path;}
std::filesystem::path trace_root(){auto path=data_dir()/L"sessions";std::filesystem::create_directories(path);return path;}
Records read_records(const std::filesystem::path& path){auto data=read_shared_file(path);if(data.empty())throw std::runtime_error("Cannot read session record");return decode(data);}

bool alive(DWORD pid,unsigned long long created){Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid)};return process.h&&birth(process.h)==created&&WaitForSingleObject(process.h,0)==WAIT_TIMEOUT;}
bool same_user(HANDLE target){
    Handle self_token,target_token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&self_token.h)||!OpenProcessToken(target,TOKEN_QUERY,&target_token.h))return false;
    auto sid=[](HANDLE token){DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<BYTE> result(bytes);if(!GetTokenInformation(token,TokenUser,result.data(),bytes,&bytes))throw std::runtime_error("Cannot verify process owner");return result;};auto a=sid(self_token.h),b=sid(target_token.h);return EqualSid(reinterpret_cast<TOKEN_USER*>(a.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid)!=FALSE;
}
void validate_target(const Process& process,HANDLE target){
    if(!target||birth(target)!=process.created||process.pid==GetCurrentProcessId()||!same_user(target))throw std::runtime_error("Game process changed or belongs to another user");DWORD own_session=0,target_session=0;ProcessIdToSessionId(GetCurrentProcessId(),&own_session);ProcessIdToSessionId(process.pid,&target_session);if(own_session!=target_session)throw std::runtime_error("Services and other sessions are protected");
    BOOL critical=TRUE;using Critical=BOOL(WINAPI*)(HANDLE,PBOOL);auto check=reinterpret_cast<Critical>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"IsProcessCritical"));if(!check||!check(target,&critical)||critical)throw std::runtime_error("Critical process is protected");auto path=lower(executable(process.pid));wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);if(path.rfind(lower(utf8(windows))+"\\",0)==0)throw std::runtime_error("Windows processes are protected");for(auto name:{"antivirus","defender","security","crowdstrike","sentinel","sophos"})if(path.find(name)!=std::string::npos)throw std::runtime_error("Security processes are protected");
}
std::string restore_guard(const std::filesystem::path& file,bool check_parent){
    if(std::filesystem::weakly_canonical(file.parent_path())!=std::filesystem::weakly_canonical(guard_root()))throw std::runtime_error("Recovery record is outside this user's guard folder");auto rows=read_records(file);if(rows.size()!=1||rows[0].size()!=10||rows[0][0]!="priority-guard"||rows[0][1]!="1")throw std::runtime_error("Invalid game recovery record");auto& row=rows[0];if(row[2]=="restored")return "Already restored";
    if(check_parent&&alive(static_cast<DWORD>(std::stoul(row[3])),std::stoull(row[4])))throw std::runtime_error("This session owner is still running; stop the session first");
    DWORD pid=static_cast<DWORD>(std::stoul(row[5]));auto created=std::stoull(row[6]);DWORD before=static_cast<DWORD>(std::stoul(row[7])),after=static_cast<DWORD>(std::stoul(row[8]));
    if(before!=IDLE_PRIORITY_CLASS&&before!=BELOW_NORMAL_PRIORITY_CLASS&&before!=NORMAL_PRIORITY_CLASS&&before!=ABOVE_NORMAL_PRIORITY_CLASS&&before!=HIGH_PRIORITY_CLASS)throw std::runtime_error("Unsafe saved process priority");
    Handle target{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_SET_INFORMATION|SYNCHRONIZE,FALSE,pid)};
    if(target.h&&birth(target.h)==created&&WaitForSingleObject(target.h,0)==WAIT_TIMEOUT){auto current=GetPriorityClass(target.h);if(current!=before){if(current!=after)throw std::runtime_error("Priority changed outside NovaTune; original state retained for review");if(!SetPriorityClass(target.h,before))throw std::runtime_error(error());}}
    row[2]="restored";atomic_write(file,encode(rows));return "Restored original priority or game already exited";
}
}
PriorityGuard::PriorityGuard(const Process& process,DWORD priority,const std::filesystem::path& recovery){
    if(priority!=NORMAL_PRIORITY_CLASS&&priority!=ABOVE_NORMAL_PRIORITY_CLASS&&priority!=HIGH_PRIORITY_CLASS)throw std::runtime_error("Unsupported game priority");
    if(std::filesystem::weakly_canonical(recovery.parent_path())!=std::filesystem::weakly_canonical(guard_root())||std::filesystem::exists(recovery))throw std::runtime_error("Choose a new private game recovery record");
    Handle target{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_SET_INFORMATION|SYNCHRONIZE,FALSE,process.pid)};validate_target(process,target.h);auto before=GetPriorityClass(target.h);if(!before||before==REALTIME_PRIORITY_CLASS)throw std::runtime_error("Game priority cannot be safely changed");
    auto self_birth=birth(GetCurrentProcess());Records record{{"priority-guard","1","prepared",std::to_string(GetCurrentProcessId()),std::to_string(self_birth),std::to_string(process.pid),std::to_string(process.created),std::to_string(before),std::to_string(priority),executable(process.pid)}};atomic_write(recovery,encode(record));
    SECURITY_ATTRIBUTES sa{sizeof sa,nullptr,TRUE};Handle parent,inherited_target,ready,mapping;
    DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&parent.h,SYNCHRONIZE,TRUE,0);DuplicateHandle(GetCurrentProcess(),target.h,GetCurrentProcess(),&inherited_target.h,0,TRUE,DUPLICATE_SAME_ACCESS);
    stop=CreateEventW(&sa,TRUE,FALSE,nullptr);ready.h=CreateEventW(&sa,TRUE,FALSE,nullptr);mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(Shared),nullptr);
    if(!parent.h||!inherited_target.h||!stop||!ready.h||!mapping.h){if(stop)CloseHandle(stop);stop=nullptr;throw std::runtime_error(error());}
    auto shared=static_cast<Shared*>(MapViewOfFile(mapping.h,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!shared){CloseHandle(stop);stop=nullptr;throw std::runtime_error(error());}shared->result=0;
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);std::wstring command=quote(self)+L" --priority-worker";HANDLE handles[]={parent.h,inherited_target.h,stop,ready.h,mapping.h};for(auto handle:handles)command+=L" "+std::to_wstring(reinterpret_cast<UINT_PTR>(handle));command+=L" "+quote(recovery.wstring());
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<BYTE> memory(size);startup.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)){UnmapViewOfFile(shared);CloseHandle(stop);stop=nullptr;throw std::runtime_error(error());}
    UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof handles,nullptr,nullptr);PROCESS_INFORMATION info{};
    auto launched=CreateProcessW(self,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&info);auto code=GetLastError();DeleteProcThreadAttributeList(startup.lpAttributeList);
    if(!launched){UnmapViewOfFile(shared);CloseHandle(stop);stop=nullptr;throw std::runtime_error(error(code));}child=info.hProcess;CloseHandle(info.hThread);
    auto wait=WaitForSingleObject(ready.h,5000);auto result=shared->result;UnmapViewOfFile(shared);if(wait!=WAIT_OBJECT_0||result!=1){SetEvent(stop);WaitForSingleObject(child,5000);CloseHandle(child);CloseHandle(stop);child=stop=nullptr;throw std::runtime_error("Priority watchdog could not start; recovery record kept");}
}
PriorityGuard::~PriorityGuard(){if(stop)SetEvent(stop);if(child){WaitForSingleObject(child,5000);CloseHandle(child);}if(stop)CloseHandle(stop);}
int priority_worker(int argc,wchar_t** argv){
    if(argc!=8)return 1;HANDLE handles[5]{};for(int i=0;i<5;++i)handles[i]=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(argv[i+2])));auto shared=static_cast<Shared*>(MapViewOfFile(handles[4],FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!shared)return 1;
    bool success=false;auto recovery=std::filesystem::path(argv[7]);try{auto rows=read_records(recovery);if(rows.size()!=1||rows[0].size()!=10)throw std::runtime_error("Invalid guard");auto& row=rows[0];if(GetProcessId(handles[1])!=std::stoul(row[5])||birth(handles[1])!=std::stoull(row[6])||WaitForSingleObject(handles[0],0)!=WAIT_TIMEOUT)throw std::runtime_error("Process changed");
        DWORD priority=static_cast<DWORD>(std::stoul(row[8]));if(priority!=NORMAL_PRIORITY_CLASS&&priority!=ABOVE_NORMAL_PRIORITY_CLASS&&priority!=HIGH_PRIORITY_CLASS)throw std::runtime_error("Unsafe priority");if(!SetPriorityClass(handles[1],priority))throw std::runtime_error(error());row[2]="applied";atomic_write(recovery,encode(rows));success=true;
    }catch(const std::exception&){}shared->result=success?1:-1;SetEvent(handles[3]);
    if(success){HANDLE waits[]={handles[0],handles[1],handles[2]};WaitForMultipleObjects(3,waits,FALSE,INFINITE);}try{restore_guard(recovery,false);}catch(const std::exception&){}
    UnmapViewOfFile(shared);for(auto handle:handles)CloseHandle(handle);return success?0:1;
}
std::string interrupted_sessions(){std::ostringstream out;out<<"TEMPORARY SESSION RECOVERY\nOnly original process priorities changed by NovaTune guards are restored. PID creation times and current values are checked.\n\n";unsigned pending=0;for(const auto& entry:std::filesystem::directory_iterator(guard_root())){if(entry.path().extension()!=L".dat")continue;try{auto rows=read_records(entry.path());if(rows.size()==1&&rows[0].size()==10&&rows[0][2]!="restored"){++pending;out<<entry.path().filename().string()<<" / "<<rows[0][2]<<"\n"<<rows[0][9]<<"\n";}}catch(const std::exception&){out<<"Unreadable recovery record preserved: "<<entry.path().filename().string()<<"\n";}}if(!pending)out<<"No unfinished priority guards found.";return out.str()+"\n"+settings_recovery(false);}
std::string recover_interrupted_sessions(){std::ostringstream out;for(const auto& entry:std::filesystem::directory_iterator(guard_root())){if(entry.path().extension()!=L".dat")continue;try{auto rows=read_records(entry.path());if(rows.size()==1&&rows[0].size()==10&&rows[0][2]!="restored")out<<entry.path().filename().string()<<": "<<restore_guard(entry.path(),true)<<"\n";}catch(const std::exception& e){out<<entry.path().filename().string()<<": "<<e.what()<<"\n";}}return (out.str().empty()?std::string("No unfinished priority guards need recovery."):out.str())+"\n"+settings_recovery(true);}
GameSessions::~GameSessions(){try{stop("NovaTune closed");}catch(const std::exception&) {}}
void GameSessions::stop(const std::string& reason){
    settings_guard.reset();guard.reset();sleepers.clear();auto saved_trace=std::move(trace);auto saved_output=output;if(pid&&!saved_trace.empty()){saved_trace[0][6]=now();saved_trace[0][7]=reason;}pid=0;created=started=0;game.clear();title.clear();profile.clear();trace.clear();output.clear();samples=0;if(!saved_trace.empty())atomic_write(saved_output,encode(saved_trace));
}
void GameSessions::tick(const Sample& sample,const Store& store,double fps){
    if(pid&&(!alive(pid,created)||store.get("session.profile."+hex(lower(game))).empty()))stop("Game exited or profile disabled");
    if(!pid){
        std::set<std::string> names;for(const auto& row:store.rows)if(row[0]=="setting"&&row[1].rfind("session.profile.",0)==0&&!row[2].empty()){try{auto path=std::filesystem::path(wide(unhex(row[1].substr(16))));names.insert(lower(utf8(path.filename().wstring())));}catch(const std::exception&){}}
        if(names.empty())return;
        for(const auto& process:sample.processes){if(!names.count(lower(utf8(process.name))))continue;auto path=executable(process.pid);if(path.empty())continue;auto configured=store.get("session.profile."+hex(lower(path)));if(configured.empty())continue;
            if(retry_after[path]>GetTickCount64())continue;auto priority=configured=="competitive"?HIGH_PRIORITY_CLASS:configured=="streaming"?ABOVE_NORMAL_PRIORITY_CLASS:NORMAL_PRIORITY_CLASS;
            auto recovery=guard_root()/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(process.pid)+L".dat");std::unique_ptr<PriorityGuard> pending;std::unique_ptr<SettingsGuard> pending_settings;try{auto full_profile=game_profile(store,path);auto settings=profile_settings(store,full_profile);pending=std::make_unique<PriorityGuard>(process,priority,recovery);if(!settings.power.empty()||settings.reduce_animations||!settings.displays.empty()||!settings.playback.empty()||!settings.microphone.empty())pending_settings=std::make_unique<SettingsGuard>(process,settings);}catch(const std::exception& e){problem=e.what();retry_after[path]=GetTickCount64()+((problem.find("already active")!=std::string::npos||problem.find("changed before")!=std::string::npos)?3000:60000);continue;}problem.clear();
            pid=process.pid;created=process.created;started=GetTickCount64();game=path;title=utf8(process.name);profile=configured;guard=std::move(pending);settings_guard=std::move(pending_settings);output=trace_root()/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(pid)+L".dat");
            trace={{"session","1",game,title,profile,now(),"","running",store.get("benchmark.notes."+hex(lower(game))),store.get("performance.target."+hex(lower(game)),"0")}};
            auto slept=store.get("session.sleep."+hex(lower(game)));std::istringstream paths(slept);std::string item;std::vector<std::string> selected;while(std::getline(paths,item,';'))if(!item.empty())selected.push_back(lower(item));
            for(const auto& other:sample.processes)if(other.pid!=pid&&!selected.empty()){auto other_path=lower(executable(other.pid));if(std::find(selected.begin(),selected.end(),other_path)!=selected.end()){try{sleepers.push_back(std::make_unique<SleepSession>(other));}catch(const std::exception& e){trace.push_back({"event",now(),"Sleep refused",e.what()});}}}
            atomic_write(output,encode(trace));break;
        }
    }
    if(!pid)return;++samples;double elapsed=(GetTickCount64()-started)/1000.0;
    trace.push_back({"sample",std::to_string(elapsed),std::to_string(sample.cpu),std::to_string(sample.used),std::to_string(sample.total),std::to_string(sample.gpu_util),std::to_string(sample.disk_read),std::to_string(sample.disk_write),std::to_string(fps),std::to_string(sample.gpu_temp)});
    std::vector<Process> processes=sample.processes;std::sort(processes.begin(),processes.end(),[](const Process& a,const Process& b){return a.cpu>b.cpu;});DWORD current_session=0;ProcessIdToSessionId(GetCurrentProcessId(),&current_session);unsigned count=0;
    for(const auto& process:processes){DWORD session=0;ProcessIdToSessionId(process.pid,&session);if(process.pid==pid||session!=current_session)continue;trace.push_back({"process",std::to_string(elapsed),utf8(process.name),std::to_string(process.cpu),std::to_string(process.ram)});if(++count==5)break;}
    if(trace.size()>24000)trace.erase(trace.begin()+1,trace.begin()+7);if(samples%5==0)atomic_write(output,encode(trace));
}
std::string session_history(bool timeline,const std::string& game,size_t limit){
    std::vector<std::filesystem::path> files;for(const auto& entry:std::filesystem::directory_iterator(trace_root()))if(entry.path().extension()==L".dat")files.push_back(entry.path());std::sort(files.rbegin(),files.rend());std::ostringstream out;out<<(timeline?"BACKGROUND ACTIVITY TIMELINE":"GAME SESSION HISTORY")<<"\nLocal CPU/RAM samples, not a proof of per-frame causation. Per-process network counters are not fabricated.\n\n";unsigned count=0;
    for(const auto& file:files){if(count>=std::min<size_t>(20,limit))break;try{auto rows=read_records(file);if(rows.empty()||rows[0].size()!=10||rows[0][0]!="session")continue;const auto& head=rows[0];if(!game.empty()&&lower(head[2])!=lower(game))continue;++count;out<<head[3]<<" / "<<head[4]<<"\nStart: "<<head[5]<<"  End: "<<(head[6].empty()?"not finalized (still running or interrupted)":head[6])<<"\nStatus: "<<head[7]<<"\nNotes: "<<head[8]<<"\n";
        double cpu=0,ram=0,gpu=-1,fps=0;unsigned samples=0,frames=0;for(size_t index=game.empty()?1:std::max<size_t>(1,rows.size()>180?rows.size()-180:1);index<rows.size();++index){const auto& row=rows[index];if(row.empty())continue;if(row[0]=="sample"&&row.size()==10){++samples;cpu=std::max(cpu,std::stod(row[2]));ram=std::max(ram,std::stod(row[3]));gpu=std::max(gpu,std::stod(row[5]));double value=std::stod(row[8]);if(value>0){fps+=value;++frames;}if(timeline)out<<"  "<<row[1]<<" s / CPU "<<row[2]<<"% / RAM "<<row[3]<<" GB / GPU "<<(std::stod(row[5])<0?std::string("unavailable"):row[5]+"%")<<" / FPS "<<(value>0?row[8]:"unavailable")<<"\n";}
            else if(timeline&&row[0]=="process"&&row.size()==5)out<<"    "<<row[1]<<" s / "<<row[2]<<" / CPU "<<row[3]<<"% / RAM "<<row[4]<<" MB\n";}
        out<<"Samples "<<samples<<" / peak CPU "<<cpu<<"% / peak RAM "<<ram<<" GB / peak reported GPU "<<(gpu<0?std::string("unavailable"):std::to_string(gpu)+"%")<<"\n";if(frames){double average=fps/frames;out<<"Mean sampled overlay FPS "<<average<<" (not a full per-frame benchmark)\n";try{auto target=decode(head[9]);if(target.size()==1&&target[0].size()==3){double goal=std::stod(target[0][1]);out<<"Sampled average FPS goal "<<goal<<": "<<(average>=goal?"met":"below goal")<<"\n1% low goals require a per-frame benchmark, not these samples.\n";}}catch(const std::exception&) {}}out<<"\n";
    }catch(const std::exception& e){out<<"Unreadable session preserved: "<<e.what()<<"\n";}}
    if(!count)out<<"No opted-in game sessions recorded for this selection yet.";return out.str();
}
}
