#pragma once
#include "windows.hpp"
#include <atomic>
#include <thread>
namespace hr {
struct StressSnapshot {
    bool running=false;
    unsigned elapsed=0,duration=0,workers=0;
    unsigned long long cpu_rounds=0,ram_bytes=0,gpu_dispatches=0,errors=0;
    std::string gpu_status;
};
class StressTest {
    std::thread worker;
    std::atomic<bool> stopping{false},running{false};
    std::atomic<unsigned long long> cpu{0},ram{0},gpu{0},errors{0};
    std::atomic<unsigned> elapsed{0};
    mutable std::mutex mutex;
    unsigned duration=0,workers=0;
    std::string gpu_status;
public:
    ~StressTest();
    void start(unsigned seconds,bool cpu_load,bool ram_load,bool gpu_load);
    void stop();
    StressSnapshot snapshot() const;
};
}
