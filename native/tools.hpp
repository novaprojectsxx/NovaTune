#pragma once
#include "windows.hpp"
#include <atomic>
#include <deque>
#include <thread>
namespace hr {
double trim_ram(const Process& process);
Process own_process();
struct TempFile {
    std::filesystem::path path;
    unsigned long long size, modified, identity;
    DWORD volume;
};
struct CleanupResult { size_t deleted=0, skipped=0; unsigned long long bytes=0; };
std::filesystem::path temp_root();
std::vector<TempFile> scan_temp(const std::filesystem::path& root = {});
CleanupResult clean_temp(const std::vector<TempFile>& files);
void apply_fps_profile(Store& store);
void reduce_suggestions(Store& store);
void undo_registry(const Row& row);
Records optional_apps();
void remove_optional_package(const Row& app);
void remove_optional_app(Store& store, const Row& app);
void restore_optional_app(const Row& journal);
class LiveCapture {
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::atomic<double> current{0};
    std::atomic<unsigned long long> updated{0};
    mutable std::mutex mutex;
    std::string problem,captured_game,captured_metric;
    std::deque<std::pair<unsigned long long,double>> recent_frames;
public:
    std::vector<std::pair<unsigned long long,double>> frames() const;
    std::string game() const;
    std::string metric() const;
    ~LiveCapture();
    void start(const std::string& executable,const std::string& game);
    void stop();
    double fps() const;
    std::string error() const;
};
}
