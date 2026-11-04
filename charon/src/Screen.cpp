#include "Screen.hpp"

#include <Paint.hpp>

#include <cairo.h>

#include <algorithm>
#include <cmath>

namespace charon {

using namespace cerberus::paint;

namespace {

constexpr double RowHeight = 64;
constexpr double ListWidth = 360;
constexpr double MenuItem = 38;
constexpr const char* Small = "Sans 11";

void addTarget(std::vector<Target>* targets, Target::Kind kind, int index, double x, double y, double w, double h)
{
    if (targets)
        targets->push_back({kind, index, x, y, w, h});
}

bool hovered(const Screen& screen, Target::Kind kind, int index = 0)
{
    return screen.hover.kind == kind && screen.hover.index == index;
}

// A round button `size` across, centred on `cx`, `cy`, lit while the pointer is on it.
void roundButton(cairo_t* cr, double cx, double cy, double size, bool lit)
{
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, size / 2, 0, 2 * M_PI);
    setColor(cr, lit ? Hover : Field);
    cairo_fill(cr);
}

// The power symbol: a ring open at the top, with a bar through the opening.
void powerSymbol(cairo_t* cr, double cx, double cy, double radius)
{
    setColor(cr, Text);
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, radius, -M_PI / 2 + 0.75, 3 * M_PI / 2 - 0.75);
    cairo_stroke(cr);
    cairo_move_to(cr, cx, cy - radius - 1.5);
    cairo_line_to(cr, cx, cy - 1);
    cairo_stroke(cr);
}

// A chevron pointing left, as a back button has it, or down, as a drop-down has it.
void chevron(cairo_t* cr, double cx, double cy, double size, bool down)
{
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_new_path(cr);
    if (down) {
        cairo_move_to(cr, cx - size, cy - size / 2);
        cairo_line_to(cr, cx, cy + size / 2);
        cairo_line_to(cr, cx + size, cy - size / 2);
    } else {
        cairo_move_to(cr, cx + size / 2, cy - size);
        cairo_line_to(cr, cx - size / 2, cy);
        cairo_line_to(cr, cx + size / 2, cy + size);
    }
    cairo_stroke(cr);
}

// A popover of `items` with its top left at `x`, `y`; the current one, if any, in bold.
void menu(cairo_t* cr, const Screen& screen, double x, double y, double width, const std::vector<std::string>& items,
    int current, Target::Kind kind, std::vector<Target>* targets)
{
    const double height = items.size() * MenuItem + 12;
    roundedRect(cr, x + 0.5, y + 0.5, width - 1, height - 1, 12);
    setColor(cr, Field);
    cairo_fill_preserve(cr);
    setColor(cr, Border);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const double top = y + 6 + i * MenuItem;
        if (hovered(screen, kind, int(i)) || screen.menuSelected == int(i)) {
            roundedRect(cr, x + 6, top, width - 12, MenuItem, 8);
            setColor(cr, Hover);
            cairo_fill(cr);
        }
        setColor(cr, Text);
        leftAligned(cr, items[i], int(i) == current ? "Sans Bold 11" : Small, x + 18, top + MenuItem / 2);
        addTarget(targets, kind, int(i), x + 6, top, width - 12, MenuItem);
    }
}

void drawUsers(cairo_t* cr, int width, int height, const Screen& screen, std::vector<Target>* targets)
{
    const double centreX = width / 2.0;
    const int count = int(screen.people.size());
    // As many rows as take less than half the height, around the one selected.
    const int shown = std::clamp(int(height * 0.45 / RowHeight), 1, std::max(count, 1));
    const int first = std::clamp(screen.selected - shown / 2, 0, std::max(0, count - shown));
    const double x = centreX - ListWidth / 2;
    double y = height * 0.48 - std::min(shown, count) * RowHeight / 2;
    for (int i = first; i < std::min(count, first + shown); ++i, y += RowHeight) {
        const bool lit = hovered(screen, Target::Kind::User, i);
        // Lighter than the circles of initials, which show on it.
        if (lit || i == screen.selected) {
            roundedRect(cr, x, y, ListWidth, RowHeight, 14);
            setColor(cr, Hover, i == screen.selected ? 1 : 0.6);
            cairo_fill(cr);
        }
        const auto& person = screen.people[std::size_t(i)];
        avatar(cr, x + 12 + 24, y + 8, 48, person.name, person.picture);
        setColor(cr, Text);
        leftAligned(cr, person.name, "Sans Bold 12", x + 76, y + RowHeight / 2);
        addTarget(targets, Target::Kind::User, i, x, y, ListWidth, RowHeight);
    }
    // Others log in by name.
    const std::string other = "Not listed?";
    const Size size = measure(cr, other, Small);
    y += 18;
    setColor(cr, hovered(screen, Target::Kind::NotListed) ? Text : DimText);
    centred(cr, other, Small, centreX, y);
    addTarget(
        targets, Target::Kind::NotListed, 0, centreX - size.width / 2.0 - 8, y - 6, size.width + 16, size.height + 12);
}

// The field, with the way back at its left; returns where the next line goes.
double drawField(cairo_t* cr, const Screen& screen, double centreX, double y, std::vector<Target>* targets)
{
    const double fieldX = centreX - FieldWidth / 2;
    Entry field;
    field.placeholder = screen.question;
    field.typed = screen.typed;
    field.text = screen.text;
    field.visible = !screen.secret;
    field.focused = screen.focused;
    field.caret = screen.caret;
    field.checking = screen.checking;
    field.pulse = screen.pulse;
    field.wrong = screen.wrong;
    entry(cr, fieldX, y, field);

    const double backX = fieldX - 14 - FieldHeight / 2;
    roundButton(cr, backX, y + FieldHeight / 2, FieldHeight - 6, hovered(screen, Target::Kind::Back));
    setColor(cr, Text);
    chevron(cr, backX - 1, y + FieldHeight / 2, 5, false);
    addTarget(targets, Target::Kind::Back, 0, backX - FieldHeight / 2, y, FieldHeight, FieldHeight);
    return y + FieldHeight + 14;
}

void drawPrompt(cairo_t* cr, int width, int height, const Screen& screen, std::vector<Target>* targets)
{
    const double centreX = width / 2.0;
    // A third of the way down, or higher on a short screen, for the rest to fit under it.
    double y = std::max(70.0, std::min(height * 0.36, height - 360.0));
    if (screen.stage == Screen::Stage::Prompt) {
        // As the lock screen has it: their picture, their name, then what PAM asks.
        constexpr double Avatar = 96;
        avatar(cr, centreX, y, Avatar, screen.person.name, screen.person.picture);
        y += Avatar + 14;
        setColor(cr, Text);
        y += centred(cr, screen.person.name, "Sans Bold 15", centreX, y) + 16;
    } else {
        y += 40;
        setColor(cr, Text);
        y += centred(cr, "Who is logging in?", "Sans Bold 15", centreX, y) + 16;
    }
    y = drawField(cr, screen, centreX, y, targets);

    std::vector<Screen::Line> lines = screen.messages;
    if (screen.checking && lines.empty())
        lines.push_back({"Checking…", false});
    else if (screen.capsLock && screen.secret && !screen.checking)
        lines.push_back({"Caps Lock is on", false});
    for (const auto& line : lines) {
        setColor(cr, line.error ? Error : DimText);
        y += centred(cr, line.text, Small, centreX, y) + 6;
    }
    if (screen.stage != Screen::Stage::Prompt)
        return;

    // The session to start, when there is a choice, in a drop-down under it all.
    y += 10;
    if (screen.sessions.size() > 1) {
        const std::string name
            = screen.sessions[std::size_t(std::clamp(screen.session, 0, int(screen.sessions.size()) - 1))];
        const Size size = measure(cr, name, Small);
        const double pillWidth = size.width + 56;
        const double pillHeight = 34;
        const double x = centreX - pillWidth / 2;
        roundedRect(cr, x, y, pillWidth, pillHeight, pillHeight / 2);
        setColor(cr, hovered(screen, Target::Kind::Session) || screen.sessionsOpen ? Hover : Field);
        cairo_fill(cr);
        setColor(cr, Text);
        leftAligned(cr, name, Small, x + 18, y + pillHeight / 2);
        chevron(cr, x + pillWidth - 22, y + pillHeight / 2, 4, true);
        addTarget(targets, Target::Kind::Session, 0, x, y, pillWidth, pillHeight);
        y += pillHeight + 10;
    }
    if (!screen.keyboardLayout.empty()) {
        setColor(cr, DimText);
        y += centred(cr, "Keyboard: " + screen.keyboardLayout, Small, centreX, y) + 6;
    }
}

} // namespace

void draw(cairo_t* cr, int width, int height, const Screen& screen, bool main, std::vector<Target>* targets)
{
    setColor(cr, Background);
    cairo_paint(cr);
    if (!main)
        return;
    const double centreX = width / 2.0;

    setColor(cr, DimText);
    centred(cr, screen.clock, "Sans Bold 13", centreX, 24);

    // Suspending, restarting and turning off, in a menu at the top right, as GNOME has it.
    constexpr double Button = 36;
    const double powerX = width - 20 - Button / 2;
    const double powerY = 20 + Button / 2;
    if (!screen.powerActions.empty()) {
        roundButton(cr, powerX, powerY, Button, hovered(screen, Target::Kind::Power) || screen.powerOpen);
        powerSymbol(cr, powerX, powerY, 7);
        addTarget(targets, Target::Kind::Power, 0, powerX - Button / 2, powerY - Button / 2, Button, Button);
    }

    if (screen.stage == Screen::Stage::Users)
        drawUsers(cr, width, height, screen, targets);
    else
        drawPrompt(cr, width, height, screen, targets);

    if (!screen.notice.empty()) {
        setColor(cr, DimText);
        centred(cr, screen.notice, "Sans 10", centreX, height - 40);
    }

    // Menus over the rest, and taking clicks before it.
    if (screen.sessionsOpen && targets) {
        const auto pill = std::ranges::find(*targets, Target::Kind::Session, &Target::kind);
        if (pill != targets->end()) {
            constexpr double MenuWidth = 260;
            const double menuHeight = screen.sessions.size() * MenuItem + 12;
            const double below = pill->y + pill->height + 6;
            const double top = below + menuHeight <= height - 8 ? below : pill->y - 6 - menuHeight;
            menu(cr, screen, centreX - MenuWidth / 2, top, MenuWidth, screen.sessions, screen.session,
                Target::Kind::SessionItem, targets);
        }
    }
    if (screen.powerOpen) {
        constexpr double MenuWidth = 200;
        menu(cr, screen, width - 20 - MenuWidth, powerY + Button / 2 + 8, MenuWidth, screen.powerActions, -1,
            Target::Kind::PowerItem, targets);
    }
}

} // namespace charon
