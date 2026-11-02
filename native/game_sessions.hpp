#pragma once
#include "windows.hpp"
#include "session.hpp"
#include "devices.hpp"
namespace hr {
class PriorityGuard {
    HANDLE stop=nullptr,child=nullptr;
public:
    PriorityGuard(const Process& process,DWORD priority,const std::filesystem::path& recovery);
    ~PriorityGuard();
};
int priority_worker(int argc,wchar_t** argv);
std::string interrupted_sessions();
std::string recover_interrupted_sessions();
class GameSessions {
    std::unique_ptr<PriorityGuard> guard;
    std::unique_ptr<SettingsGuard> settings_guard;
    std::vector<std::unique_ptr<SleepSession>> sleepers;
    DWORD pid=0;unsigned long long created=0,started=0;
    std::string game,title,profile;
    Records trace;
    std::filesystem::path output;
    unsigned samples=0;std::map<std::string,unsigned long long> retry_after;std::string problem;
public:
    ~GameSessions();
    void tick(const Sample& sample,const Store& store,double fps);
    void stop(const std::string& reason="stopped");
    bool active()const{return pid!=0;}
    std::string error()const{return problem;}
    std::string active_game()const{return game;}
    unsigned long long active_id()const{return started;}
    void add_event(const std::string& kind,const std::string& detail){if(pid)trace.push_back({"event",now(),kind,detail});}
};
std::string session_history(bool timeline=false,const std::string& game="",size_t limit=20);
}
