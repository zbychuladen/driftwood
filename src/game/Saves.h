// Save slots on disk: a quick-save slot and a few numbered ones, in the
// usual place for a game's files on each platform. A file is written beside
// its slot and then renamed over it, so a crash mid-save never damages the
// save that was there.
#pragma once
#include <string>
#include <vector>
#include "sim/Sim.h"

namespace saves {

constexpr int QUICK = 0, SLOTS = 5;   // slot 0 is the quick save, 1..SLOTS the numbered ones

// Where saves live: DRIFTWOOD_SAVE_DIR if set, otherwise the platform's place
// for app data. setDir() overrides both (for --save-dir and tests).
std::string dir();
void setDir(const std::string& d);

struct Slot {
    int n = 0;
    std::string name, path;      // "Quick save" or "Slot 2"; the file
    bool used = false, damaged = false;
    dw::Sim::SaveInfo info;      // when used and readable
    std::string when;            // when it was saved, as the player reads it
};
std::vector<Slot> list();
bool any();                      // is there anything to load?

bool write(int slot, const std::string& data, std::string* error);
bool read(int slot, std::string& data, std::string* error);

} // namespace saves
