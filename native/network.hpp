#pragma once
#include <atomic>
#include <functional>
#include <string>
namespace hr {
struct NetworkProgress {std::string phase="Ready";double mbps=0,download=-1,upload=-1,latency=-1,jitter=-1;unsigned long long bytes=0;};
double network_mbps(unsigned long long bytes,double seconds);
std::string internet_speed(bool standard,std::atomic<bool>& cancel,const std::function<void(const NetworkProgress&)>& progress={});
std::string network_health();
}
