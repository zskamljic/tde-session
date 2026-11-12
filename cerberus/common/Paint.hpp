#pragma once

#include <cstddef>
#include <string>

typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

// How the lock screen and the login screen draw: TDE's dark colours, text, and the rounded
// field a password is typed into. The two look alike, so that one is known from the other.
namespace cerberus::paint {

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
constexpr Color Hover = rgb(0x383d4a);
constexpr Color Border = rgb(0x3b4252);
constexpr Color Text = rgb(0xd3dae3);
constexpr Color DimText = rgb(0x8a939f);
constexpr Color Accent = rgb(0x5294e2);
constexpr Color Error = rgb(0xfc4138);

constexpr double FieldWidth = 320;
constexpr double FieldHeight = 44;

void setColor(cairo_t* cr, const Color& color, double alpha = 1);

// The size `text` takes in `font`, a Pango font description such as "Sans 11".
struct Size {
    int width = 0;
    int height = 0;
};
Size measure(cairo_t* cr, const std::string& text, const char* font);
// `text` in `font`, centred on `centreX` with its top at `top`; returns its height.
double centred(cairo_t* cr, const std::string& text, const char* font, double centreX, double top);
// `text` in `font` from `x`, centred on `middle` from top to bottom; returns its width.
double leftAligned(cairo_t* cr, const std::string& text, const char* font, double x, double middle);

void roundedRect(cairo_t* cr, double x, double y, double width, double height, double radius);

// A round picture of someone, `size` across with its top in the middle at `centreX`, `top`:
// their own `picture` when there is one, otherwise the first letter of `name`.
void avatar(
    cairo_t* cr, double centreX, double top, double size, const std::string& name, cairo_surface_t* picture = nullptr);

// What a field to type into shows.
struct Entry {
    std::string placeholder; // shown while nothing is typed
    std::size_t typed = 0; // characters of a secret, shown as dots
    std::string text; // what was typed of an answer that may be seen; empty for a secret
    bool visible = false; // the answer is seen as typed, rather than as dots
    bool focused = false; // keys come here; a ring of the accent goes around it
    bool caret = true; // shown at this blink, while focused
    bool checking = false; // three dots lit in turn, in place of what was typed
    unsigned pulse = 0; // which of those dots is lit
    bool wrong = false; // drawn in the colour of errors
    bool selected = false; // all that is typed, as Ctrl+A selects it
};

// A field `FieldWidth` wide and `FieldHeight` high, round at the ends, with its top left
// corner at `x`, `y`.
void entry(cairo_t* cr, double x, double y, const Entry& entry);

} // namespace cerberus::paint
