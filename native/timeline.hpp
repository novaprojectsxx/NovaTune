#pragma once
#include <vector>
#include <utility>
#include <cstddef>
namespace hr {
struct TimelineBin {double maximum=0;bool present=false;};
std::vector<TimelineBin> timeline_bins(const std::vector<std::pair<unsigned long long,double>>& frames,unsigned long long begin,unsigned long long end,size_t pixels);
size_t nearest_tick(const std::vector<unsigned long long>& ticks,unsigned long long target,unsigned long long tolerance);
}
