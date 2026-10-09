#pragma once

#include "wlr.hpp"

namespace atlas {

class View;

// What a window shows, alone, for ext-image-copy-capture: its parts drawn over nothing, so
// rounded corners and whatever else is see-through about it stays so, at the scale of the
// screen it is on. The capture wlroots makes of a scene node draws over black instead.
class WindowCapture {
public:
    explicit WindowCapture(View& view);
    ~WindowCapture();

    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    wlr_ext_image_capture_source_v1* source() { return &m_source.base; }
    // The window as it is now, in a buffer locked for the caller, with where it is drawn
    // relative to the window in `extents`; null when it shows nothing.
    wlr_buffer* snapshot(wlr_box& extents);

    // For wlroots, through the source --------------------------------------------------

    static WindowCapture& fromSource(wlr_ext_image_capture_source_v1* source);
    // A copy starts: the copies are told the size the window is drawn at.
    void start();
    // A copy waits for a frame: the window is drawn, and the copies take it.
    void frame();
    void copy(wlr_ext_image_copy_capture_frame_v1* frame, wlr_buffer* buffer);

private:
    // The source as wlroots knows it, with the way back to this.
    struct Source {
        wlr_ext_image_capture_source_v1 base;
        WindowCapture* owner;
    };

    // Where the window's parts are, in the coordinates of its capture tree.
    wlr_box extents() const;
    double scale() const;
    // The window drawn into a buffer of the swapchain; null when there is nothing to draw.
    wlr_buffer* render();

    View& m_view;
    Source m_source {};
    wlr_swapchain* m_swapchain = nullptr; // of the size the window is drawn at
    wlr_box m_extents {};
    double m_scale = 1;
};

} // namespace atlas
