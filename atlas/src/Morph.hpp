#pragma once

#include "Slices.hpp"
#include "wlr.hpp"

#include <array>

namespace atlas {

// A picture of a window, drawn as one of another size would look: see slices().
class SlicedPicture {
public:
    // `extents` are where the picture is relative to the window, which was `width` by `height`.
    SlicedPicture(wlr_scene_tree* parent, wlr_buffer* buffer, const wlr_box& extents, int width, int height);
    ~SlicedPicture();

    SlicedPicture(const SlicedPicture&) = delete;
    SlicedPicture& operator=(const SlicedPicture&) = delete;

    wlr_scene_node* node() { return &m_tree->node; }
    // Draws it as the window would be at `box`, in layout coordinates.
    void place(const wlr_box& box);
    void setOpacity(float opacity);

private:
    wlr_scene_tree* m_tree;
    std::array<wlr_scene_buffer*, 9> m_parts {};
    wlr_box m_extents;
    int m_width;
    int m_height;
    double m_scaleX; // pixels of the buffer for one of the layout
    double m_scaleY;
};

} // namespace atlas
