#include "Morph.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace atlas {
namespace {

// How far from each side of a window stays as it is: title bars with their buttons, and the
// edges of what is below them.
constexpr int BandX = 160;
constexpr int BandY = 64;

} // namespace

SlicedPicture::SlicedPicture(wlr_scene_tree* parent, wlr_buffer* buffer, const wlr_box& extents, int width, int height)
    : m_tree(wlr_scene_tree_create(parent))
    , m_extents(extents)
    , m_width(width)
    , m_height(height)
    , m_scaleX(double(buffer->width) / std::max(extents.width, 1))
    , m_scaleY(double(buffer->height) / std::max(extents.height, 1))
{
    for (wlr_scene_buffer*& part : m_parts)
        part = wlr_scene_buffer_create(m_tree, buffer);
}

SlicedPicture::~SlicedPicture()
{
    wlr_scene_node_destroy(&m_tree->node);
}

void SlicedPicture::place(const wlr_box& box)
{
    wlr_scene_node_set_position(&m_tree->node, box.x, box.y);
    const auto columns = slices(m_extents.x, m_extents.width, m_width, box.width, BandX);
    const auto rows = slices(m_extents.y, m_extents.height, m_height, box.height, BandY);
    for (size_t row = 0; row < rows.size(); ++row) {
        for (size_t column = 0; column < columns.size(); ++column) {
            wlr_scene_buffer* part = m_parts[row * columns.size() + column];
            Slice x = columns[column];
            Slice y = rows[row];
            // Within the picture, whatever it held.
            for (auto [slice, length] : {std::pair {&x, m_extents.width}, {&y, m_extents.height}}) {
                slice->from = std::clamp(slice->from, 0.0, double(length));
                slice->length = std::min(slice->length, length - slice->from);
            }
            // A size of 0 would draw the whole buffer.
            const bool shown = x.size > 0 && y.size > 0 && x.length > 0 && y.length > 0;
            wlr_scene_node_set_enabled(&part->node, shown);
            if (!shown)
                continue;
            const wlr_fbox source {x.from * m_scaleX, y.from * m_scaleY, x.length * m_scaleX, y.length * m_scaleY};
            wlr_scene_buffer_set_source_box(part, &source);
            wlr_scene_buffer_set_dest_size(part, x.size, y.size);
            wlr_scene_node_set_position(&part->node, x.to, y.to);
        }
    }
}

void SlicedPicture::setOpacity(float opacity)
{
    for (wlr_scene_buffer* part : m_parts)
        wlr_scene_buffer_set_opacity(part, opacity);
}

} // namespace atlas
