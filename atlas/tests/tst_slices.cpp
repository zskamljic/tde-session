#include "Slices.hpp"

#include <cstdio>
#include <cstdlib>

using atlas::Slice;
using atlas::slices;

namespace {

int failures = 0;

void expect(const char* name, const std::array<Slice, 3>& actual, const std::array<Slice, 3>& expected)
{
    if (actual == expected)
        return;
    ++failures;
    std::fprintf(stderr, "FAIL %s:\n", name);
    for (size_t i = 0; i < actual.size(); ++i) {
        std::fprintf(stderr, "  got %.1f+%.1f -> %d+%d, expected %.1f+%.1f -> %d+%d\n", actual[i].from,
            actual[i].length, actual[i].to, actual[i].size, expected[i].from, expected[i].length, expected[i].to,
            expected[i].size);
    }
}

} // namespace

int main()
{
    // The same size: the picture as it is, in three parts.
    expect("same size", slices(0, 800, 800, 800, 160),
        {Slice {0, 160, 0, 160}, Slice {160, 480, 160, 480}, Slice {640, 160, 640, 160}});

    // Growing, as to maximize: the ends stay, the middle stretches, as there is not enough of it.
    expect("growing", slices(0, 800, 800, 1600, 160),
        {Slice {0, 160, 0, 160}, Slice {160, 480, 160, 1280}, Slice {640, 160, 1440, 160}});

    // Shrinking, as from a maximized window: the middle is cut down from its middle, as it is.
    expect("shrinking", slices(0, 1600, 1600, 800, 160),
        {Slice {0, 160, 0, 160}, Slice {560, 480, 160, 480}, Slice {1440, 160, 640, 160}});

    // A shadow around the window stays beside it, as it is.
    expect("shadow", slices(-20, 840, 800, 1000, 160),
        {Slice {0, 180, -20, 180}, Slice {180, 480, 160, 680}, Slice {660, 180, 840, 180}});

    // Smaller than both ends: they meet in the middle, and nothing is between them.
    expect("tiny target", slices(0, 800, 800, 100, 160),
        {Slice {0, 50, 0, 50}, Slice {0, 0, 50, 0}, Slice {750, 50, 50, 50}});

    // A window no larger than its ends: the line where they meet is drawn wider.
    expect("tiny window", slices(0, 100, 100, 400, 160),
        {Slice {0, 50, 0, 50}, Slice {49.5, 1, 50, 300}, Slice {50, 50, 350, 50}});

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
