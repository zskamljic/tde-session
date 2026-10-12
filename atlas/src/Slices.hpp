#pragma once

#include <array>

namespace atlas {

// One of the three parts a picture is cut into along one direction: where it comes from in
// the picture and where it goes, relative to the start of the window, in layout pixels.
struct Slice {
    double from = 0; // in the picture, from its start
    double length = 0; // in the picture
    int to = 0; // from where the window starts, which the picture's start may be before
    int size = 0; // drawn; 0 when the part is left out

    bool operator==(const Slice&) const = default;
};

// How a picture `pictureStart` before the window and `pictureLength` long, of a window `window`
// long, is drawn for one `target` long: the parts within `band` of either end, title bars and
// buttons and shadows, as they are; the middle cut down to fit, from its middle, or stretched
// when there is not enough of it.
std::array<Slice, 3> slices(int pictureStart, int pictureLength, int window, int target, int band);

} // namespace atlas
