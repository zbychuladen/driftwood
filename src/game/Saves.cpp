#include "game/Saves.h"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace saves {

static std::string overrideDir;

void setDir(const std::string& d) { overrideDir = d; }

std::string dir() {
    if (!overrideDir.empty()) return overrideDir;
    if (const char* e = std::getenv("DRIFTWOOD_SAVE_DIR"); e && *e) return e;
    auto env = [](const char* k) { const char* v = std::getenv(k); return std::string(v ? v : ""); };
#if defined(_WIN32)
    std::string base = env("APPDATA");
    if (base.empty()) base = ".";
    return (fs::path(base) / "Driftwood" / "saves").string();
#elif defined(__APPLE__)
    return (fs::path(env("HOME")) / "Library" / "Application Support" / "Driftwood" / "saves").string();
#else
    std::string base = env("XDG_DATA_HOME");
    if (base.empty()) base = (fs::path(env("HOME")) / ".local" / "share").string();
    return (fs::path(base) / "driftwood" / "saves").string();
#endif
}

static std::string pathOf(int slot) { return (fs::path(dir()) / (slot == QUICK ? std::string("quick.dwsave") : "slot" + std::to_string(slot) + ".dwsave")).string(); }

static std::string whenOf(const std::string& path) {
    std::error_code ec;
    auto ft = fs::last_write_time(path, ec);
    if (ec) return "";
    // file clock to wall clock, the portable way
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%b %d, %H:%M", &tm);
    return buf;
}

static bool slurp(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::vector<Slot> list() {
    std::vector<Slot> out;
    for (int n = 0; n <= SLOTS; n++) {
        Slot s;
        s.n = n;
        s.name = n == QUICK ? "Quick save" : "Slot " + std::to_string(n);
        s.path = pathOf(n);
        std::error_code ec;
        if (fs::exists(s.path, ec)) {
            s.used = true;
            std::string data;
            s.damaged = !slurp(s.path, data) || !dw::Sim::peek(data, s.info);
            s.when = whenOf(s.path);
        }
        out.push_back(s);
    }
    return out;
}

bool any() {
    for (auto& s : list()) if (s.used && !s.damaged) return true;
    return false;
}

bool write(int slot, const std::string& data, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    std::error_code ec;
    fs::create_directories(dir(), ec);
    if (ec) return fail("Could not make the save folder: " + ec.message());
    std::string path = pathOf(slot), tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return fail("Could not write to the save folder.");
        f.write(data.data(), std::streamsize(data.size()));
        if (!f) return fail("The disk would not take the save.");
    }
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); return fail("Could not put the save in place."); }
    return true;
}

bool read(int slot, std::string& data, std::string* error) {
    if (!slurp(pathOf(slot), data)) { if (error) *error = "That slot is empty."; return false; }
    return true;
}

} // namespace saves
