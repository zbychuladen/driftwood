// What the Ledger shows, steadied. The sim's ledger() is a reading of this
// very moment (who is chopping wood right now), so on its own a gap can blink
// in and out as people come and go. The board shows a gap only once it has
// held for a while, keeps showing it for a while after it clears (marked as
// seemingly sorted), and refreshes who is suggested for it once an hour, so
// there is time to read it and act.
#pragma once
#include <string>
#include <vector>
#include "sim/Sim.h"

class LedgerBoard {
public:
    static constexpr double CONFIRM = 45, LINGER = 120, REFRESH = 60;   // game minutes
    struct Shown { dw::Gap gap; double first = 0, last = 0, candidatesAt = 0; bool sorted = false; };
    // Folds in the latest reading at game time t. Frozen (at night) it keeps what it has.
    void update(const std::vector<dw::Gap>& now, double t, bool frozen = false);
    std::vector<Shown> visible() const;   // confirmed gaps, live ones first
    void clear() { entries.clear(); }

private:
    std::vector<Shown> entries;
    double clock = 0;
};
