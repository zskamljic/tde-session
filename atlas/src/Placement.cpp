#include "Placement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace atlas {
namespace {

constexpr int Gap = 8; // between a window and the ones it is put beside
constexpr int CascadeStep = 32;

int64_t overlap(const Rect& a, const Rect& b)
{
    const int64_t width = std::min(a.right(), b.right()) - std::max(a.x, b.x);
    const int64_t height = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
    return width > 0 && height > 0 ? width * height : 0;
}

// Positions along one axis worth trying: the middle, the ends, and beside every window.
std::vector<int> candidates(int size, int start, int length, const std::vector<Rect>& others, bool horizontal)
{
    std::vector<int> positions {start + (length - size) / 2, start, start + length - size};
    for (const Rect& other : others) {
        const int low = horizontal ? other.x : other.y;
        const int high = horizontal ? other.right() : other.bottom();
        positions.push_back(high + Gap);
        positions.push_back(low - Gap - size);
    }
    // Only those that keep the window inside.
    std::erase_if(positions, [&](int p) { return p < start || p + size > start + length; });
    std::ranges::sort(positions);
    const auto [first, last] = std::ranges::unique(positions);
    positions.erase(first, last);
    return positions;
}

} // namespace

Rect placeWindow(int width, int height, const Rect& area, const std::vector<Rect>& others)
{
    // Larger than the area: from its top left corner.
    const int centreX = width <= area.width ? area.x + (area.width - width) / 2 : area.x;
    const int centreY = height <= area.height ? area.y + (area.height - height) / 2 : area.y;

    // The spot covering the least of the other windows, the nearest to the middle of those.
    Rect best {centreX, centreY, width, height};
    int64_t bestCovered = std::numeric_limits<int64_t>::max();
    double bestDistance = std::numeric_limits<double>::max();
    for (const int x :
        width <= area.width ? candidates(width, area.x, area.width, others, true) : std::vector {centreX}) {
        for (const int y :
            height <= area.height ? candidates(height, area.y, area.height, others, false) : std::vector {centreY}) {
            const Rect rect {x, y, width, height};
            int64_t covered = 0;
            for (const Rect& other : others)
                covered += overlap(rect, other);
            const double distance = std::hypot(x - centreX, y - centreY);
            if (covered < bestCovered || (covered == bestCovered && distance < bestDistance)) {
                best = rect;
                bestCovered = covered;
                bestDistance = distance;
            }
        }
    }

    // Where every spot covers as much, as under a maximized window: step aside from windows
    // already exactly there, so each one's corner shows.
    Rect rect = best;
    for (int step = 0; step < 20; ++step) {
        if (!std::ranges::any_of(others, [&](const Rect& other) { return other.x == rect.x && other.y == rect.y; }))
            break;
        rect.x += CascadeStep;
        rect.y += CascadeStep;
    }
    return rect;
}

} // namespace atlas
