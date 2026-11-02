#pragma once
#include <istream>
#include <string>
#include <vector>
namespace hr {
using Row = std::vector<std::string>;
using Records = std::vector<Row>;
std::string lower(std::string value);
std::string hex(const std::string& value);
std::string unhex(const std::string& value);
std::string encode(const Records& rows);
Records decode(const std::string& text);
struct Benchmark {
    size_t frames = 0, stutters = 0;
    double average = 0, low = 0, p95 = 0;
    std::string metric, process;
};
Benchmark calculate(std::vector<double> frames);
Benchmark csv(std::istream& stream, const std::string& process = "");
class FrameStream {
    Row header_;
    std::string expected_, stream_;
    int frame_=-1, app_=-1, dropped_=-1, pid_=-1, chain_=-1;
public:
    explicit FrameStream(std::string expected) : expected_(lower(std::move(expected))) {}
    double consume(const std::string& line);
    std::string metric()const{return frame_>=0&&static_cast<size_t>(frame_)<header_.size()?header_[frame_]:std::string();}
};
struct Shortcut { unsigned modifiers, key; };
Shortcut shortcut(const std::string& text);
}
