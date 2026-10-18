#include "game/Legacy.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "game/Saves.h"

namespace fs = std::filesystem;

namespace legacy {

std::string path() { return (fs::path(saves::dir()) / "legacy.txt").string(); }

Profile load() {
    Profile p;
    std::ifstream f(path());
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string key;
        in >> key;
        if (key == "runs") in >> p.runs;
        else if (key == "ending") { std::string e; if (in >> e) p.endings.insert(e); }
    }
    return p;
}

bool save(const Profile& p) {
    std::error_code ec;
    fs::create_directories(saves::dir(), ec);
    std::string tmp = path() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << "runs " << p.runs << "\n";
        for (auto& e : p.endings) f << "ending " << e << "\n";
        if (!f) return false;
    }
    fs::rename(tmp, path(), ec);
    return !ec;
}

const std::vector<Option>& islands() {
    static const std::vector<Option> v = {
        {"standard", "Driftwood isle", "Forest, meadow, a pond and a hill.", ""},
        {"lush", "A lush isle", "Thick with berry bushes. An easy first season.", "haven"},
        {"rocky", "A rocky isle", "Boulders everywhere: good stone, hard going.", "homeward"},
        {"misty", "A misty isle", "A mist that never lifts. Almost nothing known at the start.", "awakening"}};
    return v;
}

const std::vector<Option>& starts() {
    static const std::vector<Option> v = {
        {"ordinary", "An ordinary landing", "Five castaways, spring.", ""},
        {"winter", "A winter landing", "Wrecked as the first snow falls.", "divided"},
        {"sixth", "Six castaways", "One more pair of hands, one more mouth.", "lost"},
        {"tools", "A chest of tools", "Iron tools for the first twenty days.", "overruled"},
        {"trusted", "Trusted from the first", "They listen to the narrator from day one.", "three"}};
    return v;
}

bool unlocked(const Profile& p, const Option& o) {
    std::string n = o.needs;
    if (n.empty()) return true;
    if (n == "three") return p.endings.size() >= 3;
    return p.endings.count(n) > 0;
}

std::string unlockHint(const Option& o) {
    std::string n = o.needs;
    if (n == "three") return "Reach any three endings";
    std::string t = n;
    if (!t.empty()) t[0] = char(std::toupper(t[0]));
    return "Reach the " + t + " ending";
}

} // namespace legacy
