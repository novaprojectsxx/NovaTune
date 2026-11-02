#pragma once
#include "windows.hpp"
#include <mmreg.h>
#include <atomic>
#include <thread>
namespace hr {
struct AudioEndpoint {std::string id,name;bool capture=false;};
std::vector<AudioEndpoint> audio_endpoints();
std::string default_audio(bool capture,int role);
void set_default_audio(const std::string& id,bool capture,int role);
bool audio_switch_available();
struct AudioLevels {double peak=0,rms=0;unsigned long long samples=0,clipped=0;};
AudioLevels measure_audio(const BYTE* bytes,size_t frames,const WAVEFORMATEX& format,bool silent=false);
class MicrophoneTest {
    std::thread worker;std::atomic<bool> stopping{false},running{false};mutable std::mutex mutex;
    AudioLevels levels;std::string problem;std::filesystem::path output;unsigned long long began=0;
public:
    ~MicrophoneTest();
    void start(const std::string& endpoint,unsigned seconds=10);
    void stop();
    bool active()const{return running;}
    AudioLevels reading()const;
    std::string error()const;
    std::filesystem::path file()const;
};
}
