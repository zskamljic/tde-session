#pragma once

#include <vector>

namespace atlas {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }
    bool operator==(const Rect&) const = default;
};

// Where a new window of `width` by `height` goes in `area` beside the windows in `others`:
// in the middle when that is free, else in the free spot nearest to it, beside the other
// windows or against the edges. When there is none, where it covers the least of them; and
// when it would sit with its corner on another's, a step down and to the right of it.
Rect placeWindow(int width, int height, const Rect& area, const std::vector<Rect>& others);

} // namespace atlas
