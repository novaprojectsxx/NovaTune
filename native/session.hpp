#pragma once
#include "windows.hpp"
#include <memory>
namespace hr {
class SleepSession {
    HANDLE child=nullptr,stop_event=nullptr;
public:
    explicit SleepSession(const Process& process);
    ~SleepSession();
    SleepSession(const SleepSession&)=delete;
};
int sleep_worker(int argc,wchar_t** argv);
bool sleep_eligible(const Process& process,std::string& reason);
}
