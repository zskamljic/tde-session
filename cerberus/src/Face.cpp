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

void draw(cairo_t* cr, int width, int height, const Face& face, bool main)
{
    setColor(cr, Background);
    cairo_paint(cr);
    if (!main)
        return;
    const double centreX = width / 2.0;

    // Just the clock, and how to go on.
    if (face.curtain) {
        double y = height * 0.30;
        setColor(cr, Text);
        y += centred(cr, face.time, "Sans Light 80", centreX, y);
        setColor(cr, DimText);
        centred(cr, face.date, "Sans 17", centreX, y + 4);
        const char* hint = face.finger == Face::Finger::None ? "Press a key or click to unlock"
                                                             : "Touch the fingerprint reader or press a key to unlock";
        centred(cr, hint, "Sans 11", centreX, height * 0.85);
        return;
    }

    setColor(cr, DimText);
    centred(cr, face.time, "Sans Bold 13", centreX, 24);

    // The user's initial in a circle, the name under it, the password under that.
    double y = height * 0.36;
    constexpr double Avatar = 96;
    cairo_new_path(cr);
    cairo_arc(cr, centreX, y + Avatar / 2, Avatar / 2, 0, 2 * M_PI);
    setColor(cr, Field);
    cairo_fill(cr);
    if (!face.user.empty()) {
        // The first character, which may be several bytes.
        std::size_t length = 1;
        while (length < face.user.size() && (static_cast<unsigned char>(face.user[length]) & 0xc0) == 0x80)
            ++length;
        setColor(cr, Text);
        centred(cr, face.user.substr(0, length), "Sans 36", centreX, y + Avatar / 2 - 27);
    }
    y += Avatar + 14;
    setColor(cr, Text);
    y += centred(cr, face.user, "Sans Bold 15", centreX, y) + 16;

    const double fieldX = centreX - FieldWidth / 2;
    // Taking keys: a ring of the accent around the field, as GTK draws the focus.
    if (face.focused) {
        roundedRect(cr, fieldX - 3, y - 3, FieldWidth + 6, FieldHeight + 6, FieldHeight / 2 + 3);
        setColor(cr, face.status == Face::Status::Wrong ? Error : Accent, 0.45);
        cairo_set_line_width(cr, 3);
        cairo_stroke(cr);
    }
    roundedRect(cr, fieldX, y, FieldWidth, FieldHeight, FieldHeight / 2);
    setColor(cr, Field);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, face.focused ? 2 : 1);
    setColor(cr, face.status == Face::Status::Wrong ? Error : face.focused ? Accent : Border);
    cairo_stroke(cr);

    // A dot per character, as many as fit, and the caret after them while keys come here.
    constexpr double Dot = 4;
    constexpr double Gap = 7;
    const std::size_t room = std::size_t((FieldWidth - 48) / (2 * Dot + Gap));
    const std::size_t dots = std::min(face.typed, room);
    const double total = dots > 0 ? dots * 2 * Dot + (dots - 1) * Gap : 0;
    const double middle = y + FieldHeight / 2;
    double caretX = centreX;
    if (dots == 0) {
        setColor(cr, DimText);
        const char* placeholder = !face.focused     ? "Click here to type the password"
            : face.status == Face::Status::Checking ? ""
                                                    : "Password";
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* description = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(layout, description);
        pango_font_description_free(description);
        pango_layout_set_text(layout, placeholder, -1);
        int textWidth = 0;
        int textHeight = 0;
        pango_layout_get_pixel_size(layout, &textWidth, &textHeight);
        // From the left, as an entry starts, the caret before the words.
        const double textX = fieldX + 22;
        cairo_move_to(cr, textX, middle - textHeight / 2.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        caretX = textX - 2;
    } else {
        double x = fieldX + 22 + Dot;
        setColor(cr, Text);
        for (std::size_t i = 0; i < dots; ++i, x += 2 * Dot + Gap) {
            cairo_new_path(cr);
            cairo_arc(cr, x, middle, Dot, 0, 2 * M_PI);
            cairo_fill(cr);
        }
        caretX = fieldX + 22 + total + 5;
    }
    if (face.focused && face.caret && face.status != Face::Status::Checking) {
        setColor(cr, Text);
        cairo_rectangle(cr, std::round(caretX), middle - 10, 1.5, 20);
        cairo_fill(cr);
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
        y += centred(cr, message, "Sans 11", centreX, y) + 6;
    }

    // Under it all, the other way in.
    switch (face.finger) {
    case Face::Finger::None:
        break;
    case Face::Finger::Ready:
        setColor(cr, DimText);
        centred(cr, "Or touch the fingerprint reader", "Sans 11", centreX, y);
        break;
    case Face::Finger::NoMatch:
        setColor(cr, Error);
        centred(cr, "Fingerprint not recognized", "Sans 11", centreX, y);
        break;
    case Face::Finger::Retry:
        setColor(cr, DimText);
        centred(cr, "Touch the reader again", "Sans 11", centreX, y);
        break;
    }
}

} // namespace cerberus
