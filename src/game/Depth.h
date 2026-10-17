// Draw order for the isometric world. Every drawable has a footprint on the
// ground (in tiles) and the screen box its art covers. Two things only need an
// order when their screen boxes overlap; then the one whose footprint lies
// wholly behind the other's (smaller x or smaller y, the way the camera looks)
// is drawn first. A single depth number per object cannot do this for
// buildings that span several tiles, which is what made trees and people
// standing beside them draw on the wrong side.
#pragma once
#include <vector>

namespace depth {

struct Item {
    float x0, y0, x1, y1;       // ground footprint, tiles
    float sx0, sy0, sx1, sy1;   // screen box, world pixels
};

// -1: a is behind b, 1: a is in front of b, 0: their footprints overlap and
// order() falls back to how far forward their centres are.
int compare(const Item& a, const Item& b);

// Back-to-front drawing order, as indices into items. Never drops an item; if
// the rules ever form a loop it is broken at the item furthest back.
std::vector<int> order(const std::vector<Item>& items);

} // namespace depth
