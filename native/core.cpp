#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
namespace hr {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string hex(const std::string& s) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string unhex(const std::string& s) {
    if (s.size() % 2) throw std::runtime_error("Invalid settings encoding");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error("Invalid settings encoding");
    };
    std::string out;
    for (size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>((digit(s[i]) << 4) | digit(s[i+1]));
    return out;
}
std::string encode(const Records& rows) {
    std::string out = "HEADROOM 2\n";
    for (const auto& row : rows) {
        if (row.empty()) throw std::runtime_error("Empty settings record");
        out += std::to_string(row.size());
        for (const auto& field : row) out += "\t" + hex(field);
        out += "\n";
    }
    return out;
}
Records decode(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) || line != "HEADROOM 2")
        throw std::runtime_error("Unsupported or damaged settings file");
    Records rows;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t'), count = 0;
        try {
            size_t used;
            count = std::stoul(line.substr(0, tab), &used);
            if (used != line.substr(0, tab).size() || !count || count > 64) throw std::runtime_error("");
        } catch (...) { throw std::runtime_error("Invalid settings record"); }
        Row row;
        if (tab == std::string::npos) throw std::runtime_error("Incomplete settings record");
        size_t start = tab + 1;
        for (;;) {
            size_t end = line.find('\t', start);
            row.push_back(unhex(line.substr(start, end == std::string::npos ? end : end - start)));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (row.size() != count) throw std::runtime_error("Incomplete settings record");
        rows.push_back(std::move(row));
    }
    return rows;
}
Benchmark calculate(std::vector<double> frames) {
    frames.erase(std::remove_if(frames.begin(), frames.end(), [](double v) {
        return !std::isfinite(v) || v < .5 || v > 1000;
    }), frames.end());
    if (frames.size() < 60) throw std::runtime_error("At least 60 valid frames are required");
    std::sort(frames.begin(), frames.end());
    Benchmark b;
    b.frames = frames.size();
    b.average = 1000.0 * frames.size() / std::accumulate(frames.begin(), frames.end(), 0.0);
    size_t n = static_cast<size_t>(std::ceil(frames.size() * .01));
    b.low = 1000.0 * n / std::accumulate(frames.end() - n, frames.end(), 0.0);
    b.p95 = frames[static_cast<size_t>(std::ceil(frames.size() * .95)) - 1];
    b.stutters = std::count_if(frames.begin(), frames.end(), [](double v) { return v >= 50; });
    return b;
}
                                                                         
static bool csvrow(std::istream& in, Row& row) {
    row.clear();
    std::string field;
    bool quoted = false, ended = false, any = false;
    char c;
    while (in.get(c)) {
        any = true;
        if (quoted) {
            if (c == '"') {
                if (in.peek() == '"') { in.get(c); field += '"'; }
                else { quoted = false; ended = true; }
            } else field += c;
        } else if (c == ',') { row.push_back(field); field.clear(); ended = false; }
        else if (c == '\n' || c == '\r') {
            if (c == '\r' && in.peek() == '\n') in.get(c);
            row.push_back(field); return true;
        } else if (c == '"' && field.empty() && !ended) quoted = true;
        else {
            if (ended || c == '"') throw std::runtime_error("Malformed CSV quoting");
            field += c;
        }
    }
    if (quoted) throw std::runtime_error("Unterminated CSV field");
    if (any) row.push_back(field);
    return any;
}
Benchmark csv(std::istream& stream, const std::string& expected) {
    Row header, row;
    if (stream.peek() == 0xef) {
        char bom[3]{};
        stream.read(bom, 3);
        if (std::string(bom, 3) != "\xef\xbb\xbf") throw std::runtime_error("Invalid CSV byte order mark");
    }
    if (!csvrow(stream, header)) throw std::runtime_error("Empty CSV");
    if (header[0].compare(0, 3, "\xef\xbb\xbf") == 0) header[0].erase(0, 3);
    auto column = [&](const std::string& key) -> int {
        auto it = std::find(header.begin(), header.end(), key);
        return it == header.end() ? -1 : static_cast<int>(it - header.begin());
    };
    int frame = -1;
    for (const auto& key : {"MsBetweenDisplayChange", "MsBetweenPresents", "msBetweenPresents"})
        if ((frame = column(key)) >= 0) break;
    if (frame < 0) throw std::runtime_error("No supported frame interval column; capture PresentMon v1 metrics");
    int app = column("Application"), dropped = column("Dropped");
    int pid = column("ProcessID"), chain = column("SwapChainAddress");
    if (!expected.empty() && app < 0) throw std::runtime_error("Application column required for game filtering");
    std::vector<double> values;
    std::set<std::string> processes, streams;
    while (csvrow(stream, row)) {
        auto get = [&](int i) { return i >= 0 && static_cast<size_t>(i) < row.size() ? row[i] : ""; };
        auto process = lower(get(app));
        if (!expected.empty() && process != lower(expected)) continue;
        auto drop = lower(get(dropped));
        if (drop == "1" || drop == "true") continue;
        try {
            size_t used;
            auto text = get(frame);
            double v = std::stod(text, &used);
            if (used != text.size() || !std::isfinite(v) || v < .5 || v > 1000) continue;
            values.push_back(v);
        } catch (const std::exception&) { continue; }
        if (!process.empty()) processes.insert(process);
        streams.insert(get(pid) + ":" + get(chain));
    }
    if (processes.size() > 1) throw std::runtime_error("Multiple applications in CSV; select a game first");
    if (streams.size() > 1) throw std::runtime_error("Multiple process/swap-chain streams; export a single stream before importing");
    auto b = calculate(std::move(values));
    b.metric = header[frame];
    b.process = processes.empty() ? "" : *processes.begin();
    return b;
}
double FrameStream::consume(const std::string& line) {
    std::istringstream input(line); Row row;
    if (!csvrow(input,row)) return 0;
    if (header_.empty()) {
        auto column=[&](const std::string& key)->int {
            auto found=std::find(row.begin(),row.end(),key);
            return found==row.end()?-1:static_cast<int>(found-row.begin());
        };
        for (const auto& field:{"MsBetweenDisplayChange","MsBetweenPresents","msBetweenPresents"})
            if ((frame_=column(field))>=0) break;
        if(frame_<0) return 0;
        app_=column("Application");dropped_=column("Dropped");pid_=column("ProcessID");chain_=column("SwapChainAddress");
        header_=row;return 0;
    }
    auto get=[&](int i){return i>=0 && static_cast<size_t>(i)<row.size()?row[i]:std::string();};
    if(!expected_.empty() && lower(get(app_))!=expected_) return 0;
    auto dropped=lower(get(dropped_));if(dropped=="1" || dropped=="true")return 0;
    double ms=0;
    try {size_t n;auto text=get(frame_);ms=std::stod(text,&n);
        if(n!=text.size() || !std::isfinite(ms) || ms<.5 || ms>1000)return 0;
    }catch(const std::exception&){return 0;}
    auto id=get(pid_)+":"+get(chain_);
    if(stream_.empty())stream_=id;
    if(id!=stream_)return 0;
    return ms;
}
Shortcut shortcut(const std::string& text) {
    std::istringstream input(lower(text));
    std::vector<std::string> parts;
    std::string part;
    while (std::getline(input, part, '+')) parts.push_back(part);
    if (text.empty() || text.back() == '+' || parts.size() < 2) throw std::runtime_error("Use Ctrl+Alt+G or another modified letter, digit or F1-F12");
    unsigned mods = 0;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        unsigned m = parts[i] == "ctrl" ? 2 : parts[i] == "alt" ? 1 : parts[i] == "shift" ? 4 : 0;
        if (!m || (mods & m)) throw std::runtime_error("Invalid or repeated hotkey modifier");
        mods |= m;
    }
    auto key = parts.back();
    unsigned vk = 0;
    if (key.size() == 1 && ((key[0] >= 'a' && key[0] <= 'z') || (key[0] >= '0' && key[0] <= '9')))
        vk = static_cast<unsigned>(std::toupper(static_cast<unsigned char>(key[0])));
    else if (key.size() >= 2 && key[0] == 'f') {
        for (size_t i = 1; i < key.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(key[i]))) throw std::runtime_error("Invalid function key");
        int n = std::stoi(key.substr(1));
        if (n < 1 || n > 12) throw std::runtime_error("Use F1-F12");
        vk = 0x70 + n - 1;
    } else throw std::runtime_error("Invalid hotkey key");
    return {mods | 0x4000, vk};
}
}
