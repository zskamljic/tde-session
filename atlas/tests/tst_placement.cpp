#include "Placement.hpp"

#include <cstdio>
#include <cstdlib>

using atlas::placeWindow;
using atlas::Rect;

namespace {

int failures = 0;

void expect(const char* name, const Rect& actual, const Rect& expected)
{
    if (actual == expected)
        return;
    ++failures;
    std::fprintf(stderr, "FAIL %s: got %d,%d %dx%d, expected %d,%d %dx%d\n", name, actual.x, actual.y, actual.width,
        actual.height, expected.x, expected.y, expected.width, expected.height);
}

} // namespace

int main()
{
    const Rect screen {0, 34, 1920, 1000};

    expect("empty screen: the middle", placeWindow(800, 600, screen, {}), {560, 234, 800, 600});

    // The middle taken: the nearest free spot beside the window there, the left one of two
    // as near.
    const Rect middle {560, 234, 800, 600};
    expect("beside the middle one", placeWindow(400, 300, screen, {middle}), {152, 384, 400, 300});

    // Left and right taken, and the middle: above it, the upper of two as near.
    const Rect left {0, 34, 552, 1000};
    const Rect right {1368, 34, 552, 1000};
    expect("between two", placeWindow(400, 300, screen, {left, right, {560, 334, 800, 400}}), {760, 34, 400, 300});

    // No free spot: where it covers the least, against the edges.
    expect("least covered", placeWindow(800, 600, screen, {middle}), {0, 34, 800, 600});

    // Every spot as bad, as under a maximized window: the middle, then a step aside from a
    // window with the same corner.
    const Rect full {0, 34, 1920, 1000};
    expect("full: the middle", placeWindow(800, 600, screen, {full}), {560, 234, 800, 600});
    const Rect small {0, 0, 800, 600};
    expect("full: cascaded", placeWindow(800, 600, small, {small}), {32, 32, 800, 600});

    // Larger than the screen: its top left corner.
    expect("too large", placeWindow(2400, 1200, screen, {}), {0, 34, 2400, 1200});

    if (failures == 0)
        std::puts("all placement tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
