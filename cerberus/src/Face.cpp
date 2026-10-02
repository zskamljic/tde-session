#include "Face.hpp"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>

namespace cerberus {
namespace {

struct Color {
    double r, g, b;
};

constexpr Color rgb(unsigned hex)
{
    return {((hex >> 16) & 0xff) / 255.0, ((hex >> 8) & 0xff) / 255.0, (hex & 0xff) / 255.0};
}

// The colours of TDE's dark theme.
constexpr Color Background = rgb(0x262a33);
constexpr Color Field = rgb(0x2f343f);
constexpr Color Border = rgb(0x3b4252);
constexpr Color Text = rgb(0xd3dae3);
constexpr Color DimText = rgb(0x8a939f);
constexpr Color Accent = rgb(0x5294e2);
constexpr Color Error = rgb(0xfc4138);

constexpr double FieldWidth = 320;
constexpr double FieldHeight = 44;

void setColor(cairo_t* cr, const Color& color, double alpha = 1)
{
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
}

// `text` in `font`, centred on `centreX` with its top at `top`; returns its height.
double centred(cairo_t* cr, const std::string& text, const char* font, double centreX, double top)
{
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* description = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, description);
    pango_font_description_free(description);
    pango_layout_set_text(layout, text.c_str(), -1);
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(cr, centreX - width / 2.0, top);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    return height;
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

} // namespace

void draw(cairo_t* cr, int width, int height, const Face& face)
{
    setColor(cr, Background);
    cairo_paint(cr);

    const double centreX = width / 2.0;
    // The clock above the middle, the password just below it.
    double y = height * 0.28;
    setColor(cr, Text);
    y += centred(cr, face.time, "Sans Light 64", centreX, y);
    setColor(cr, DimText);
    y += centred(cr, face.date, "Sans 15", centreX, y + 4);

    y = height * 0.55;
    setColor(cr, Text);
    y += centred(cr, face.user, "Sans Bold 13", centreX, y) + 12;

    const double fieldX = centreX - FieldWidth / 2;
    roundedRect(cr, fieldX, y, FieldWidth, FieldHeight, FieldHeight / 2);
    setColor(cr, Field);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 2);
    setColor(cr, face.status == Face::Status::Wrong ? Error : face.typed > 0 ? Accent : Border);
    cairo_stroke(cr);

    if (face.typed == 0) {
        setColor(cr, DimText);
        centred(cr, "Enter Password", "Sans 11", centreX, y + FieldHeight / 2 - 9);
    } else {
        // A dot per character, as many as fit.
        constexpr double Dot = 4;
        constexpr double Gap = 7;
        const std::size_t room = std::size_t((FieldWidth - 40) / (2 * Dot + Gap));
        const std::size_t dots = std::min(face.typed, room);
        const double total = dots * 2 * Dot + (dots - 1) * Gap;
        double x = centreX - total / 2 + Dot;
        setColor(cr, Text);
        for (std::size_t i = 0; i < dots; ++i, x += 2 * Dot + Gap) {
            cairo_new_path(cr);
            cairo_arc(cr, x, y + FieldHeight / 2, Dot, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    y += FieldHeight + 14;

    std::string message;
    Color color = DimText;
    switch (face.status) {
    case Face::Status::Checking:
        message = "Checking…";
        break;
    case Face::Status::Wrong:
        message = "Wrong password";
        color = Error;
        break;
    case Face::Status::Ready:
        if (face.capsLock)
            message = "Caps Lock is on";
        break;
    }
    if (!message.empty()) {
        setColor(cr, color);
        centred(cr, message, "Sans 11", centreX, y);
    }
}

} // namespace cerberus
