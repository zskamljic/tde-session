#include "Decoration.hpp"

#include "Parts.hpp"
#include "View.hpp"

#include <cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>

namespace atlas {
namespace {

constexpr int ButtonSize = 26;
constexpr int ButtonGap = 6;
constexpr int SideMargin = 8;
constexpr int CornerRadius = 8;
// Within this distance of a corner, dragging the side resizes both ways.
constexpr int CornerReach = 20;

struct Color {
    double r, g, b, a = 1;
};

constexpr Color rgb(uint32_t hex, double alpha = 1)
{
    return {((hex >> 16) & 0xff) / 255.0, ((hex >> 8) & 0xff) / 255.0, (hex & 0xff) / 255.0, alpha};
}

// The colours of the dark theme of TDE's applications, so these title bars look like theirs.
constexpr Color Header = rgb(0x2f343f);
constexpr Color Border = rgb(0x262a33);
constexpr Color Text = rgb(0xd3dae3);
constexpr Color DimText = rgb(0x8a939f);
constexpr Color Hover = {1, 1, 1, 18 / 255.0};
constexpr Color Pressed = {1, 1, 1, 34 / 255.0};
constexpr Color CloseHover = rgb(0xcc575d);

void setColor(cairo_t* cr, const Color& color)
{
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
}

// Pixels drawn with cairo, handed to the scene as a buffer it can read.
struct CairoBuffer {
    wlr_buffer base;
    cairo_surface_t* surface;

    static CairoBuffer* from(wlr_buffer* buffer) { return reinterpret_cast<CairoBuffer*>(buffer); }
};
static_assert(offsetof(CairoBuffer, base) == 0);

const wlr_buffer_impl CairoBufferImpl {
    .destroy =
        [](wlr_buffer* buffer) {
            cairo_surface_destroy(CairoBuffer::from(buffer)->surface);
            delete CairoBuffer::from(buffer);
        },
    .get_dmabuf = nullptr,
    .get_shm = nullptr,
    .begin_data_ptr_access =
        [](wlr_buffer* buffer, uint32_t flags, void** data, uint32_t* format, size_t* stride) {
            if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
                return false;
            cairo_surface_t* surface = CairoBuffer::from(buffer)->surface;
            *data = cairo_image_surface_get_data(surface);
            *format = DRM_FORMAT_ARGB8888;
            *stride = size_t(cairo_image_surface_get_stride(surface));
            return true;
        },
    .end_data_ptr_access = [](wlr_buffer*) { },
};

void roundedTop(cairo_t* cr, double width, double height, double radius)
{
    cairo_new_path(cr);
    cairo_move_to(cr, 0, height);
    cairo_line_to(cr, 0, radius);
    cairo_arc(cr, radius, radius, radius, M_PI, 1.5 * M_PI);
    cairo_line_to(cr, width - radius, 0);
    cairo_arc(cr, width - radius, radius, radius, 1.5 * M_PI, 2 * M_PI);
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);
}

} // namespace

Decoration::Decoration(View& view)
    : m_view(view)
{
    // Below the window, reaching out of it: what the pointer finds there resizes it.
    static const float transparent[4] {0, 0, 0, 0};
    m_margin = wlr_scene_rect_create(view.tree, 1, 1, transparent);
    wlr_scene_node_lower_to_bottom(&m_margin->node);
    wlr_scene_node_set_position(&m_margin->node, -ResizeMargin, -ResizeMargin);
    m_bar = wlr_scene_buffer_create(view.tree, nullptr);
    m_captureBar = wlr_scene_buffer_create(view.captureTree(), nullptr);
    update();
}

Decoration::~Decoration()
{
    wlr_scene_node_destroy(&m_margin->node);
    wlr_scene_node_destroy(&m_bar->node);
    wlr_scene_node_destroy(&m_captureBar->node);
}

void Decoration::setVisible(bool visible)
{
    wlr_scene_node_set_enabled(&m_margin->node, visible);
    wlr_scene_node_set_enabled(&m_bar->node, visible);
    wlr_scene_node_set_enabled(&m_captureBar->node, visible);
}

Decoration::State Decoration::current() const
{
    const wlr_box frame = m_view.geometry();
    float scale = 1;
    if (const Output* output = m_view.server.outputAt(frame.x + frame.width / 2.0, frame.y + frame.height / 2.0))
        scale = output->output->scale;
    return State {
        .width = frame.width,
        .scale = scale,
        .title = m_view.title(),
        .activated = m_view.activated,
        .square = m_view.isTiled(),
        .maximized = m_view.tile == Tile::Maximized,
        .hovered = m_hovered,
        .pressed = m_pressed,
    };
}

void Decoration::update()
{
    const State state = current();
    const wlr_box frame = m_view.geometry();
    wlr_scene_rect_set_size(
        m_margin, std::max(1, frame.width + 2 * ResizeMargin), std::max(1, frame.height + 2 * ResizeMargin));
    if (m_hasDrawn && state == m_drawn)
        return;
    if (state.width > 0)
        draw(state);
}

wlr_box Decoration::buttonBox(Part part, int width) const
{
    // Minimize, maximize and close from left to right, at the right end.
    int index = 0;
    switch (part) {
    case Part::Close:
        index = 0;
        break;
    case Part::Maximize:
        index = 1;
        break;
    case Part::Minimize:
        index = 2;
        break;
    default:
        return {};
    }
    const int x = width - SideMargin - ButtonSize - index * (ButtonSize + ButtonGap);
    return {x, (TitleHeight - ButtonSize) / 2, ButtonSize, ButtonSize};
}

void Decoration::draw(const State& state)
{
    const int width = int(std::ceil(state.width * state.scale));
    const int height = int(std::ceil(TitleHeight * state.scale));
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, state.scale, state.scale);

    // The bar, its top corners rounded unless the window fills a tile of the screen.
    roundedTop(cr, state.width, TitleHeight, state.square ? 0 : CornerRadius);
    setColor(cr, Header);
    cairo_fill(cr);
    setColor(cr, Border);
    cairo_rectangle(cr, 0, TitleHeight - 1, state.width, 1);
    cairo_fill(cr);

    // The title, centred over the whole width but clear of the buttons.
    const int buttons = SideMargin + 3 * ButtonSize + 2 * ButtonGap + SideMargin;
    const int titleWidth = state.width - 2 * buttons;
    if (titleWidth > 0 && !state.title.empty()) {
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* font = pango_font_description_from_string("Sans Bold 10");
        pango_layout_set_font_description(layout, font);
        pango_font_description_free(font);
        pango_layout_set_text(layout, state.title.c_str(), -1);
        pango_layout_set_width(layout, titleWidth * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        pango_layout_set_single_paragraph_mode(layout, true);
        int textWidth = 0;
        int textHeight = 0;
        pango_layout_get_pixel_size(layout, &textWidth, &textHeight);
        setColor(cr, state.activated ? Text : DimText);
        cairo_move_to(cr, buttons, (TitleHeight - textHeight) / 2.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }

    for (const Part part : {Part::Minimize, Part::Maximize, Part::Close}) {
        const wlr_box box = buttonBox(part, state.width);
        const bool hovered = state.hovered == part;
        const bool pressed = state.pressed == part && hovered;
        Color foreground = state.activated ? Text : DimText;
        const Color* background = nullptr;
        Color closeBackground = CloseHover;
        if (part == Part::Close && (hovered || pressed)) {
            if (pressed)
                closeBackground = {CloseHover.r * 0.87, CloseHover.g * 0.87, CloseHover.b * 0.87};
            background = &closeBackground;
            foreground = {1, 1, 1};
        } else if (pressed) {
            background = &Pressed;
        } else if (hovered) {
            background = &Hover;
        }

        const double cx = box.x + box.width / 2.0;
        const double cy = box.y + box.height / 2.0;
        if (background) {
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, box.width / 2.0 - 2, 0, 2 * M_PI);
            setColor(cr, *background);
            cairo_fill(cr);
        }

        setColor(cr, foreground);
        cairo_set_line_width(cr, 1.5);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        constexpr double s = 4.0;
        cairo_new_path(cr);
        switch (part) {
        case Part::Close:
            cairo_move_to(cr, cx - s, cy - s);
            cairo_line_to(cr, cx + s, cy + s);
            cairo_move_to(cr, cx - s, cy + s);
            cairo_line_to(cr, cx + s, cy - s);
            break;
        case Part::Minimize:
            cairo_move_to(cr, cx - s, cy + s - 1);
            cairo_line_to(cr, cx + s, cy + s - 1);
            break;
        case Part::Maximize:
            if (state.maximized) {
                cairo_rectangle(cr, cx - s, cy - s + 2, 2 * s - 2, 2 * s - 2);
                cairo_move_to(cr, cx - s + 2, cy - s);
                cairo_line_to(cr, cx + s, cy - s);
                cairo_line_to(cr, cx + s, cy + s - 2);
            } else {
                cairo_rectangle(cr, cx - s, cy - s, 2 * s, 2 * s);
            }
            break;
        default:
            break;
        }
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    auto* buffer = new CairoBuffer {.base = {}, .surface = surface};
    wlr_buffer_init(&buffer->base, &CairoBufferImpl, width, height);
    for (wlr_scene_buffer* node : {m_bar, m_captureBar}) {
        wlr_scene_buffer_set_buffer(node, &buffer->base);
        wlr_scene_buffer_set_dest_size(node, state.width, TitleHeight);
    }
    // The scene holds on to it as long as it shows it.
    wlr_buffer_drop(&buffer->base);
    m_drawn = state;
    m_hasDrawn = true;
}

Decoration::Part Decoration::partAt(double x, double y, uint32_t* edges) const
{
    const wlr_box frame = m_view.geometry();
    const double fx = x - frame.x;
    const double fy = y - frame.y;
    *edges = WLR_EDGE_NONE;

    const bool inside = fx >= 0 && fy >= 0 && fx < frame.width && fy < frame.height;
    if (!inside) {
        if (m_view.isTiled())
            return Part::None;
        if (fy < 0)
            *edges |= WLR_EDGE_TOP;
        else if (fy >= frame.height)
            *edges |= WLR_EDGE_BOTTOM;
        if (fx < 0)
            *edges |= WLR_EDGE_LEFT;
        else if (fx >= frame.width)
            *edges |= WLR_EDGE_RIGHT;
        // Near a corner, the side beside it counts too.
        if (!(*edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)))
            *edges |= fx < CornerReach ? WLR_EDGE_LEFT : fx >= frame.width - CornerReach ? WLR_EDGE_RIGHT : 0;
        if (!(*edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)))
            *edges |= fy < CornerReach ? WLR_EDGE_TOP : fy >= frame.height - CornerReach ? WLR_EDGE_BOTTOM : 0;
        return Part::Edge;
    }
    // The topmost pixels of the bar resize too, as the margin above them is easy to miss.
    if (fy < 3 && !m_view.isTiled()) {
        *edges = WLR_EDGE_TOP
            | (fx < CornerReach                       ? WLR_EDGE_LEFT
                    : fx >= frame.width - CornerReach ? WLR_EDGE_RIGHT
                                                      : 0);
        return Part::Edge;
    }
    if (fy >= TitleHeight)
        return Part::None;
    for (const Part part : {Part::Minimize, Part::Maximize, Part::Close}) {
        const wlr_box box = buttonBox(part, frame.width);
        if (fx >= box.x && fx < box.x + box.width && fy >= box.y && fy < box.y + box.height)
            return part;
    }
    return Part::Title;
}

void Decoration::setHovered(Part part)
{
    if (part != m_hovered) {
        m_hovered = part;
        update();
    }
}

void Decoration::setPressed(Part part)
{
    if (part != m_pressed) {
        m_pressed = part;
        update();
    }
}

} // namespace atlas
