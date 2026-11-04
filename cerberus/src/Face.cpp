#include "Face.hpp"

#include "Paint.hpp"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>

namespace cerberus {

using namespace paint;

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
    avatar(cr, centreX, y, Avatar, face.user);
    y += Avatar + 14;
    setColor(cr, Text);
    y += centred(cr, face.user, "Sans Bold 15", centreX, y) + 16;

    Entry field;
    field.placeholder = face.focused ? "Password" : "Click here to type the password";
    field.typed = face.typed;
    field.focused = face.focused;
    field.caret = face.caret;
    field.checking = face.typed == 0 && face.status == Face::Status::Checking;
    field.pulse = face.pulse;
    field.wrong = face.status == Face::Status::Wrong;
    entry(cr, centreX - FieldWidth / 2, y, field);
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
