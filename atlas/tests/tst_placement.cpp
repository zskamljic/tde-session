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

    // Other windows do not matter, unless one has its corner where this one would.
    const Rect beside {0, 34, 552, 1000};
    expect("beside another", placeWindow(800, 600, screen, {beside}), {560, 234, 800, 600});
    const Rect middle {560, 234, 800, 600};
    expect("cascaded", placeWindow(800, 600, screen, {middle}), {592, 266, 800, 600});
    expect("cascaded twice", placeWindow(800, 600, screen, {middle, {592, 266, 400, 300}}), {624, 298, 800, 600});

    // No further than the edge.
    const Rect small {0, 0, 800, 600};
    expect("no room to cascade", placeWindow(800, 600, small, {small}), {0, 0, 800, 600});

    // Larger than the screen: its top left corner.
    expect("too large", placeWindow(2400, 1200, screen, {}), {0, 34, 2400, 1200});

    if (failures == 0)
        std::puts("all placement tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
