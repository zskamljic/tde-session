#pragma once

#include <cstddef>
#include <string>

typedef struct _cairo cairo_t;

namespace cerberus {

// What the lock screen shows.
struct Face {
    enum class Status { Ready, Checking, Wrong };
    // What the fingerprint reader says, when there is one to unlock with.
    enum class Finger { None, Ready, NoMatch, Retry };

    std::string time;
    std::string date;
    std::string user;
    std::size_t typed = 0; // characters of the password, shown as dots
    Status status = Status::Ready;
    bool capsLock = false;
    Finger finger = Finger::None;
    // The clock alone, as when just locked; a key or a click lifts it to show the password.
    bool curtain = true;
    bool focused = false; // the keyboard comes here, so the password can be typed
    bool caret = true; // shown at this blink
};

// Draws `face` on an output of `width` by `height`, in its logical size: all of it on the `main`
// output, the background alone on the others.
void draw(cairo_t* cr, int width, int height, const Face& face, bool main);

} // namespace cerberus
