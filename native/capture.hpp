#pragma once
#include "windows.hpp"
#include <atomic>
#include <thread>
#include <memory>
namespace hr {
std::filesystem::path capture_folder();
struct CaptureOptions {bool cursor=true;RECT region{};std::filesystem::path output;unsigned seconds=0;};
void validate_capture_options(const CaptureOptions& options);
std::filesystem::path screenshot();
std::filesystem::path screenshot(const CaptureOptions& options);
std::string encoder_report();
std::filesystem::path crop_screenshot(const std::filesystem::path& input,const RECT& crop);
struct MediaEntry {std::filesystem::path path;unsigned long long bytes=0;std::shared_ptr<void> thumbnail;};
std::vector<MediaEntry> media_entries();
std::string capture_gallery();
double media_duration(const std::filesystem::path& input);
std::string video_information(const std::filesystem::path& input);
std::filesystem::path export_clip(const std::filesystem::path& input,double start,double end,std::atomic<bool>& cancel);
std::filesystem::path export_replay(const std::vector<std::filesystem::path>& inputs,double seconds,std::atomic<bool>& cancel);
class Recorder {
    std::thread worker;
    std::atomic<bool> stopping{false},running{false};
    std::atomic<unsigned long long> started{0},written_frames{0};
    mutable std::mutex mutex;
    std::string problem;
    std::filesystem::path output;
public:
    ~Recorder();
    void start();
    void start(const CaptureOptions& options);
    void stop();
    bool active() const{return running;}
    unsigned elapsed() const;
    unsigned long long frames()const{return written_frames;}
    std::string error() const;
    std::filesystem::path file() const;
};
struct Drive { std::wstring root, filesystem; unsigned long long free,total; };
std::vector<Drive> drives();
void optimize_drive(HWND owner,const std::wstring& root,bool analyze);
void disk_cleanup();
}
