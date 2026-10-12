#include "Slices.hpp"

#include <algorithm>

namespace atlas {

std::array<Slice, 3> slices(int pictureStart, int pictureLength, int window, int target, int band)
{
    const int before = std::max(0, -pictureStart); // shadows and the like before the window
    const int after = std::max(0, pictureStart + pictureLength - window); // and after it
    const int edge = std::max(0, std::min({band, window / 2, target / 2}));

    std::array<Slice, 3> parts;
    parts[0] = {0, double(before + edge), -before, before + edge};
    parts[2] = {double(before + window - edge), double(edge + after), target - edge, edge + after};

    Slice& middle = parts[1];
    middle.to = edge;
    middle.size = std::max(0, target - 2 * edge);
    const int available = std::max(0, window - 2 * edge);
    if (middle.size == 0) {
        middle.length = 0;
    } else if (available >= middle.size) {
        // Enough of it: as much as fits, from its middle, as it is.
        middle.from = before + edge + (available - middle.size) / 2.0;
        middle.length = middle.size;
    } else if (available > 0) {
        middle.from = before + edge;
        middle.length = available;
    } else {
        // Nothing between the ends: the line where they meet, drawn wider.
        middle.from = std::max(0.0, before + edge - 0.5);
        middle.length = 1;
    }
    return parts;
}

} // namespace atlas
