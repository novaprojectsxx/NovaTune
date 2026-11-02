#pragma once
#include "audio.hpp"
#include <condition_variable>
#include <array>
namespace hr {
struct WallpaperFrame {unsigned width=0,height=0;std::vector<BYTE> pixels;unsigned long long sequence=0;};
class WallpaperVideo {
    std::thread worker;std::atomic<bool> stopping{false},paused{false},running{false};mutable std::mutex mutex;std::condition_variable wake;WallpaperFrame frame;std::string problem;
public:
    ~WallpaperVideo();void start(const std::filesystem::path& path,int fps);void stop();void pause(bool value);WallpaperFrame latest()const;std::string error()const;bool active()const{return running;}
};
int desktop_volume();
void desktop_set_volume(int percent);
class DesktopAudioMeter {
    std::thread worker;std::atomic<bool> stopping{false};std::atomic<double> peak{0};mutable std::mutex mutex;std::string problem;std::array<std::atomic<float>,32> bins{};
public:
    std::array<float,32> waveform()const{std::array<float,32> result{};for(size_t n=0;n<32;++n)result[n]=bins[n].load();return result;}
    ~DesktopAudioMeter();void start();void stop();double level()const{return peak;}std::string error()const;
};
struct DesktopWeather {double temperature=0,wind=0;int code=0;std::string time;};
DesktopWeather parse_desktop_weather(const std::string& json);
std::string weather_description(int code);
class DesktopWeatherClient {
    std::thread worker;std::atomic<bool> busy{false};mutable std::mutex mutex;DesktopWeather reading;std::string problem="Weather off / choose coordinates and Refresh";bool valid=false;
public:
    ~DesktopWeatherClient();void refresh(double latitude,double longitude);bool active()const{return busy;}std::string text()const;
};
}
