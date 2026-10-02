#pragma once

#include "wlr.hpp"

#include <cstdint>
#include <string>

namespace atlas {

class View;

// The title bar the compositor draws for windows that do not draw their own: X11 programs and
// Wayland programs that ask for it. It shows the title and the buttons to minimize, maximize
// and close, and around the window lies a margin to resize it by.
class Decoration {
public:
    static constexpr int TitleHeight = 36;
    static constexpr int ResizeMargin = 8; // outside the window, where dragging resizes it

    enum class Part { None, Title, Minimize, Maximize, Close, Edge };

    explicit Decoration(View& view);
    ~Decoration();

    Decoration(const Decoration&) = delete;
    Decoration& operator=(const Decoration&) = delete;

    // Draws the title bar again when anything it shows has changed.
    void update();
    void setVisible(bool visible);

    // The part at `x`, `y` in layout coordinates; for an edge, which ones in `edges`.
    Part partAt(double x, double y, uint32_t* edges) const;
    void setHovered(Part part);
    void setPressed(Part part);
    Part pressed() const { return m_pressed; }

private:
    struct State {
        int width = 0;
        float scale = 1;
        std::string title;
        bool activated = false;
        bool square = false;
        bool maximized = false;
        Part hovered = Part::None;
        Part pressed = Part::None;
        bool operator==(const State&) const = default;
    };

    State current() const;
    void draw(const State& state);
    wlr_box buttonBox(Part part, int width) const;

    View& m_view;
    wlr_scene_rect* m_margin;
    wlr_scene_buffer* m_bar;
    wlr_scene_buffer* m_captureBar;
    State m_drawn;
    bool m_hasDrawn = false;
    Part m_hovered = Part::None;
    Part m_pressed = Part::None;
};

} // namespace atlas
