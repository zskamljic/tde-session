#include "Placement.hpp"

#include <algorithm>

namespace atlas {
namespace {

constexpr int CascadeStep = 32;

} // namespace

Rect placeWindow(int width, int height, const Rect& area, const std::vector<Rect>& others)
{
    Rect rect {
        width <= area.width ? area.x + (area.width - width) / 2 : area.x,
        height <= area.height ? area.y + (area.height - height) / 2 : area.y,
        width,
        height,
    };
    for (int step = 0; step < 20; ++step) {
        if (!std::ranges::any_of(others, [&](const Rect& other) { return other.x == rect.x && other.y == rect.y; }))
            break;
        // Not past the edges, as long as it fits.
        if (rect.right() + CascadeStep > area.right() || rect.bottom() + CascadeStep > area.bottom())
            break;
        rect.x += CascadeStep;
        rect.y += CascadeStep;
    }
    return rect;
}

} // namespace atlas
