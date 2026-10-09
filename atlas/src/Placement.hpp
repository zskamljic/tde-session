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

// Where a new window of `width` by `height` goes in `area`: in the middle, or a step down and
// to the right of a window of `others` with its corner there already, so each one's corner
// shows. One larger than the area goes to its top left corner.
Rect placeWindow(int width, int height, const Rect& area, const std::vector<Rect>& others);

} // namespace atlas
