// Places name labels so none overlap: each tries its own spot, then up to
// three rows below or above it; a label with nowhere free is hidden. The pinned label
// (the selected settler) is placed first and always shown.
#pragma once
#include <vector>

namespace labels {

struct Box { float x, y, w, h; };
struct Want { Box at; bool pinned = false; };

// For each wanted label, where it goes, or h == 0 when it is hidden.
std::vector<Box> layout(const std::vector<Want>& wants);

bool overlap(const Box& a, const Box& b);

} // namespace labels
