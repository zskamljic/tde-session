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

// Draws what `paint` draws on the cairo_t it is given, faded to `alpha` and moved down by
// `dy`, or nothing when it would not be seen.
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

#include <cmath>

namespace cerberus {

template <typename Paint> void faded(cairo_t* cr, double alpha, double dy, Paint paint)
{
    if (alpha <= 0.001)
        return;
    if (alpha >= 0.999 && dy == 0) {
        paint(cr);
        return;
    }
    cairo_save(cr);
    cairo_translate(cr, 0, dy);
    // Faded as a whole, but only over as much of the screen as it covers: a group the size of
    // the screen, twice over every frame, is more than a laptop draws in time. What it covers
    // is found by drawing it first where nothing is painted.
    cairo_matrix_t matrix;
    cairo_get_matrix(cr, &matrix);
    cairo_surface_t* recording = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
    cairo_t* measure = cairo_create(recording);
    cairo_set_matrix(measure, &matrix);
    paint(measure);
    cairo_destroy(measure);
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    cairo_recording_surface_ink_extents(recording, &x, &y, &width, &height);
    cairo_surface_destroy(recording);
    if (width <= 0 || height <= 0) {
        cairo_restore(cr);
        return;
    }
    // Text there is laid out without hinting, so a little to spare.
    constexpr double Margin = 16;
    cairo_identity_matrix(cr);
    cairo_rectangle(cr, std::floor(x - Margin), std::floor(y - Margin), std::ceil(width + 2 * Margin),
        std::ceil(height + 2 * Margin));
    cairo_clip(cr);
    cairo_set_matrix(cr, &matrix);
    cairo_push_group(cr);
    paint(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

} // namespace cerberus
