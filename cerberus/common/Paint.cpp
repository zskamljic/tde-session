#include "Paint.hpp"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>

namespace cerberus::paint {
namespace {

PangoLayout* layoutOf(cairo_t* cr, const std::string& text, const char* font)
{
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* description = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, description);
    pango_font_description_free(description);
    pango_layout_set_text(layout, text.c_str(), -1);
    return layout;
}

// The first character of `text`, which may be several bytes.
std::string initial(const std::string& text)
{
    std::size_t length = 1;
    while (length < text.size() && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80)
        ++length;
    return text.substr(0, std::min(length, text.size()));
}

} // namespace

void setColor(cairo_t* cr, const Color& color, double alpha)
{
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
}

Size measure(cairo_t* cr, const std::string& text, const char* font)
{
    PangoLayout* layout = layoutOf(cr, text, font);
    Size size;
    pango_layout_get_pixel_size(layout, &size.width, &size.height);
    g_object_unref(layout);
    return size;
}

double centred(cairo_t* cr, const std::string& text, const char* font, double centreX, double top)
{
    PangoLayout* layout = layoutOf(cr, text, font);
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(cr, centreX - width / 2.0, top);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    return height;
}

double leftAligned(cairo_t* cr, const std::string& text, const char* font, double x, double middle)
{
    PangoLayout* layout = layoutOf(cr, text, font);
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(cr, x, middle - height / 2.0);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    return width;
}

void roundedRect(cairo_t* cr, double x, double y, double width, double height, double radius)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -M_PI / 2, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, M_PI / 2);
    cairo_arc(cr, x + radius, y + height - radius, radius, M_PI / 2, M_PI);
    cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void avatar(cairo_t* cr, double centreX, double top, double size, const std::string& name, cairo_surface_t* picture)
{
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, centreX, top + size / 2, size / 2, 0, 2 * M_PI);
    if (picture) {
        // Scaled to cover the circle, cut to it.
        cairo_clip(cr);
        const int width = cairo_image_surface_get_width(picture);
        const int height = cairo_image_surface_get_height(picture);
        const double scale = size / std::max(1, std::min(width, height));
        cairo_translate(cr, centreX - width * scale / 2, top + size / 2 - height * scale / 2);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, picture, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
        return;
    }
    setColor(cr, Field);
    cairo_fill(cr);
    if (!name.empty()) {
        setColor(cr, Text);
        // As large as it was at 96 across, for any size.
        const std::string font = "Sans " + std::to_string(int(std::lround(size * 36 / 96)));
        centred(cr, initial(name), font.c_str(), centreX, top + size / 2 - size * 27 / 96);
    }
    cairo_restore(cr);
}

void entry(cairo_t* cr, double x, double y, const Entry& entry)
{
    // Taking keys: a ring of the accent around the field, as GTK draws the focus.
    if (entry.focused) {
        roundedRect(cr, x - 3, y - 3, FieldWidth + 6, FieldHeight + 6, FieldHeight / 2 + 3);
        setColor(cr, entry.wrong ? Error : Accent, 0.45);
        cairo_set_line_width(cr, 3);
        cairo_stroke(cr);
    }
    roundedRect(cr, x, y, FieldWidth, FieldHeight, FieldHeight / 2);
    setColor(cr, Field);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, entry.focused ? 2 : 1);
    setColor(cr, entry.wrong ? Error : entry.focused ? Accent : Border);
    cairo_stroke(cr);

    constexpr double Dot = 4;
    constexpr double Gap = 7;
    constexpr double Inset = 22; // from the round end to where the text starts
    const double centreX = x + FieldWidth / 2;
    const double middle = y + FieldHeight / 2;
    double caretX = x + Inset - 2;
    if (entry.checking) {
        // Three dots in the middle, lit in turn, while what was typed is checked.
        for (int i = 0; i < 3; ++i) {
            setColor(cr, entry.pulse % 3 == unsigned(i) ? Text : DimText);
            cairo_new_path(cr);
            cairo_arc(cr, centreX + (i - 1) * (2 * Dot + Gap), middle, Dot, 0, 2 * M_PI);
            cairo_fill(cr);
        }
        return;
    }
    if (entry.visible && !entry.text.empty()) {
        // As much of the end as fits, as an entry scrolls along with the caret.
        constexpr const char* Font = "Sans 11";
        const double room = FieldWidth - 2 * Inset;
        const double width = measure(cr, entry.text, Font).width;
        const double start = x + Inset - std::max(0.0, width - room);
        cairo_save(cr);
        cairo_rectangle(cr, x + Inset - 1, y, room + 2, FieldHeight);
        cairo_clip(cr);
        if (entry.selected) {
            roundedRect(cr, std::max(start, x + Inset) - 2, middle - 11, std::min(width, room) + 4, 22, 4);
            setColor(cr, Accent);
            cairo_fill(cr);
        }
        setColor(cr, Text);
        leftAligned(cr, entry.text, Font, start, middle);
        cairo_restore(cr);
        caretX = start + width + 1;
    } else if (!entry.visible && entry.typed > 0) {
        // A dot per character, as many as fit, and the caret after them.
        const std::size_t room = std::size_t((FieldWidth - 48) / (2 * Dot + Gap));
        const std::size_t dots = std::min(entry.typed, room);
        double dotX = x + Inset + Dot;
        if (entry.selected) {
            // Behind the dots, as a selection is behind text.
            const double width = dots * 2 * Dot + (dots - 1) * Gap;
            roundedRect(cr, x + Inset - 4, middle - Dot - 5, width + 8, 2 * Dot + 10, 4);
            setColor(cr, Accent);
            cairo_fill(cr);
        }
        setColor(cr, Text);
        for (std::size_t i = 0; i < dots; ++i, dotX += 2 * Dot + Gap) {
            cairo_new_path(cr);
            cairo_arc(cr, dotX, middle, Dot, 0, 2 * M_PI);
            cairo_fill(cr);
        }
        caretX = x + Inset + (dots * 2 * Dot + (dots - 1) * Gap) + 5;
    } else {
        // From the left, as an entry starts, the caret before the words.
        setColor(cr, DimText);
        leftAligned(cr, entry.placeholder, "Sans 11", x + Inset, middle);
    }
    if (entry.focused && entry.caret) {
        setColor(cr, Text);
        cairo_rectangle(cr, std::round(caretX), middle - 10, 1.5, 20);
        cairo_fill(cr);
    }
}

} // namespace cerberus::paint
