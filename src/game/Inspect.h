// The info card for whatever the player clicked: a title, what kind of thing
// it is, a short description and a few facts from the simulation. Reads sim
// state only, so tests can check every kind without a window.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "game/WorldView.h"
#include "sim/Sim.h"

struct Info {
    std::string title, kind, desc;
    std::vector<std::pair<std::string, std::string>> rows;   // label, value
};

Info describe(dw::Sim& sim, const Pick& p);
