#pragma once

#include "Owned.hpp"

typedef struct _cairo cairo_t;

namespace cerberus {

// The curtain of the lock and login screens going up and down: the clock slides up and fades
// as what is under it slides up into place, as GNOME's do. Its timer goes off while it moves;
// whoever polls it calls step() then, and draws again.
class Lift {
public:
    static constexpr int Duration = 300; // ms

    Lift();

    // Starts towards lifted, from wherever it is; nothing happens if it is going there already.
    void set(bool lifted);
    bool lifted() const { return m_lifted; }
    // Moves on with the time gone by; false once it has stopped.
    bool step();
    // How far up it is, eased, from 0 (the clock) to 1 (what is under it).
    double value() const;
    int fd() const { return m_timer.get(); }

private:
    void startTimer(bool on);

    FileDescriptor m_timer;
    bool m_lifted = false;
    double m_from = 0; // where it was when it last started moving
    double m_at = 0; // how far it has gone, not eased
    long long m_started = 0; // ms, monotonic
};

// Draws what `paint` draws, faded to `alpha` and moved down by `dy`, or nothing when it would
// not be seen.
template <typename Paint> void faded(cairo_t* cr, double alpha, double dy, Paint paint);

// How far a lift has gone, as each part shows it: the clock fades out and rises by `rise`,
// what is under it fades in and rises from `rise` below its place.
struct Shown {
    double alpha;
    double dy;
};
Shown clockShown(double lift, double rise);
Shown underShown(double lift, double rise);

} // namespace cerberus

#include <cairo.h>

namespace cerberus {

template <typename Paint> void faded(cairo_t* cr, double alpha, double dy, Paint paint)
{
    if (alpha <= 0.001)
        return;
    if (alpha >= 0.999 && dy == 0) {
        paint();
        return;
    }
    cairo_push_group(cr);
    cairo_translate(cr, 0, dy);
    paint();
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
}

} // namespace cerberus
