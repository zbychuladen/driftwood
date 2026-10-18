// The legacy between runs: which endings have been reached, kept in a small
// profile beside the saves, and what they unlock for the next new game: other
// kinds of island and other starting situations. Mostly variety, a little
// help. A running game never changes; only a new one starts differently.
#pragma once
#include <set>
#include <string>
#include <vector>

namespace legacy {

struct Profile {
    std::set<std::string> endings;   // haven, homeward, awakening, overruled, lost, divided
    int runs = 0;                    // new games started
};

// The profile file (legacy.txt in the save folder). load() gives an empty
// profile when there is none or it cannot be read.
std::string path();
Profile load();
bool save(const Profile& p);

// A choice for a new game, and what unlocks it: an ending, "three" for any
// three different endings, or "" from the start.
struct Option { const char* key; const char* name; const char* desc; const char* needs; };
const std::vector<Option>& islands();
const std::vector<Option>& starts();
bool unlocked(const Profile& p, const Option& o);
std::string unlockHint(const Option& o);   // "Reach the Haven ending"

} // namespace legacy
