// Driftwood: an isometric island colony sim where you are the unseen narrator.
//
// Usage: driftwood [--seed N] [--size WxH] [--fullscreen]
// Capture mode (for docs/CI): --screenshot file.png [--days N] [--hour H] [--zoom Z] [--select K] [--tab T]
//                             [--center X,Y] (tile) [--pick X,Y] (screen point) [--show-journal] [--links] [--gallery] [--people] [--buildings] [--bench N]
#include <cstdlib>
#include <cstring>
#include <string>
#include "game/Game.h"

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--seed") o.seed = uint32_t(std::strtoul(next(), nullptr, 10));
        else if (a == "--size") { std::string s = next(); auto x = s.find('x'); if (x != std::string::npos) { o.width = std::atoi(s.c_str()); o.height = std::atoi(s.c_str() + x + 1); } }
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--days") o.days = std::atoi(next());
        else if (a == "--hour") o.hour = float(std::atof(next()));
        else if (a == "--zoom") o.zoom = float(std::atof(next()));
        else if (a == "--select") o.selectFirst = std::atoi(next());
        else if (a == "--tab") o.tab = std::atoi(next());
        else if (a == "--show-journal") o.showJournal = true;
        else if (a == "--links") o.allLinks = true;
        else if (a == "--gallery") o.gallery = true;
        else if (a == "--people") o.people = true;
        else if (a == "--buildings") o.buildings = true;
        else if (a == "--bench") o.bench = std::atoi(next());
        else if (a == "--center" || a == "--pick") {
            std::string s = next();
            auto c = s.find(',');
            if (c == std::string::npos) continue;
            float x = float(std::atof(s.c_str())), y = float(std::atof(s.c_str() + c + 1));
            if (a == "--center") { o.hasCenter = true; o.centerX = x; o.centerY = y; }
            else { o.hasPick = true; o.pickX = x; o.pickY = y; }
        }
    }
    Game g(o);
    return g.run();
}
