// Draws the login screen in its stages to PNG files in the folder given, to look at.

#include "Screen.hpp"

#include <cairo.h>

#include <string>
#include <vector>

using namespace charon;

namespace {

void save(const Screen& screen, const std::string& path, int width = 1280, int height = 800)
{
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surface);
    std::vector<Target> targets;
    draw(cr, width, height, screen, true, &targets);
    cairo_destroy(cr);
    cairo_surface_write_to_png(surface, path.c_str());
    cairo_surface_destroy(surface);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string folder = argc > 1 ? argv[1] : ".";
    Screen screen;
    screen.clock = "Wed 4 Nov  12:34";
    screen.time = "12:34";
    screen.date = "Wednesday, 4 November";
    save(screen, folder + "/curtain.png");

    // Under the curtain from here on.
    screen.curtain = false;
    screen.lift = 1;
    screen.people = {{"Žan Skamljič"}, {"Guest"}, {"Ana"}};
    screen.selected = 0;
    screen.powerActions = {"Suspend", "Restart", "Power Off"};
    screen.sessions = {"GNOME", "GNOME Classic", "TDE"};
    screen.session = 2;
    screen.focused = true;
    screen.hover = {Target::Kind::User, 1};
    save(screen, folder + "/users.png");

    screen.hover = {};
    screen.stage = Screen::Stage::Prompt;
    screen.person = {"Žan Skamljič"};
    screen.question = "Password";
    screen.typed = 6;
    save(screen, folder + "/password.png");

    screen.answerSelected = true;
    save(screen, folder + "/selected.png");
    screen.answerSelected = false;

    // Halfway up, the clock going and the password coming.
    screen.curtain = true;
    screen.lift = 0.5;
    save(screen, folder + "/lifting.png");
    screen.curtain = false;
    screen.lift = 1;

    screen.typed = 0;
    screen.wrong = true;
    screen.messages = {{"Sorry, that didn’t work. Please try again.", true}};
    screen.sessionsOpen = true;
    screen.menuSelected = 2;
    screen.keyboardLayout = "Slovenian";
    save(screen, folder + "/wrong.png");

    screen.sessionsOpen = false;
    screen.wrong = false;
    screen.messages.clear();
    screen.checking = true;
    screen.powerOpen = true;
    screen.menuSelected = -1;
    screen.hover = {Target::Kind::PowerItem, 1};
    screen.notice = "Logging in leaves the keyring locked: /etc/pam.d/greetd has no pam_gnome_keyring.";
    save(screen, folder + "/checking.png");

    screen = {};
    screen.clock = "Wed 4 Nov  12:34";
    screen.curtain = false;
    screen.lift = 1;
    screen.stage = Screen::Stage::Username;
    screen.question = "Username";
    screen.secret = false;
    screen.text = "someone";
    screen.focused = true;
    save(screen, folder + "/username.png", 1024, 640);
    return 0;
}
