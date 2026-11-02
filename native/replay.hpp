#pragma once
#include "capture.hpp"
#include <condition_variable>
#include <future>
namespace hr {
class Replay {
    struct Snapshot;
    struct Request {unsigned seconds;std::promise<std::shared_ptr<Snapshot>> result;};
    std::thread worker;std::atomic<bool> stopping{false},running{false},ready_to_save{false};
    mutable std::mutex mutex;std::mutex saving;std::condition_variable wake;
    std::shared_ptr<Request> request;std::string problem;double retained=0;unsigned capacity=60;
public:
    ~Replay();
    void start(const CaptureOptions& options,unsigned seconds);
    void stop();
    bool active()const{return running;}
    bool ready()const{return running&&ready_to_save;}
    double available()const;
    std::string error()const;
    std::filesystem::path save(unsigned seconds,std::atomic<bool>& cancel);
};
}
