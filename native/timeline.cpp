#include "timeline.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace hr {
std::vector<TimelineBin> timeline_bins(const std::vector<std::pair<unsigned long long,double>>& frames,unsigned long long begin,unsigned long long end,size_t pixels){if(!pixels||pixels>16384||end<=begin)return {};std::vector<TimelineBin> bins(pixels);for(const auto& frame:frames){if(frame.first<begin||frame.first>end||!std::isfinite(frame.second)||frame.second<=0||frame.second>1000)continue;size_t column=std::min(pixels-1,static_cast<size_t>((frame.first-begin)*static_cast<double>(pixels)/(end-begin)));bins[column].present=true;bins[column].maximum=std::max(bins[column].maximum,frame.second);}return bins;}
size_t nearest_tick(const std::vector<unsigned long long>& ticks,unsigned long long target,unsigned long long tolerance){size_t result=std::numeric_limits<size_t>::max();unsigned long long distance=~0ULL;for(size_t n=0;n<ticks.size();++n){auto value=ticks[n]>target?ticks[n]-target:target-ticks[n];if(value<=tolerance&&value<distance){distance=value;result=n;}}return result;}
}
