#pragma once

#include <cstddef>
#include <string>
#include <vector>

typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

namespace charon {

// What can be clicked, and which of several it is.
struct Target {
    enum class Kind { None, User, NotListed, Back, Session, SessionItem, Power, PowerItem };
    Kind kind = Kind::None;
    int index = 0;
    double x = 0, y = 0, width = 0, height = 0;

    bool contains(double px, double py) const { return px >= x && px < x + width && py >= y && py < y + height; }
    bool same(const Target& other) const { return kind == other.kind && index == other.index; }
};

// What the login screen shows.
struct Screen {
    enum class Stage {
        Users, // the list of people, to pick who logs in
        Username, // a name typed, for one not listed
        Prompt, // PAM's questions for the one picked
    };
    struct Person {
        std::string name; // as shown
        cairo_surface_t* picture = nullptr; // owned elsewhere
    };

    Stage stage = Stage::Users;
    std::string clock; // the day and the time, at the top
    // The clock alone, as when the login screen comes; a key or a click lifts it.
    bool curtain = true;
    double lift = 0; // how far up the curtain is as it goes, from 0 down to 1 up
    std::string time; // large on the curtain
    std::string date;
    std::vector<Person> people;
    int selected = 0; // of the people, whom Enter picks
    Target hover; // under the pointer

    // The one picked, and what PAM asks of them.
    Person person;
    std::string question; // as PAM asks it, such as "Password"
    bool secret = true; // typed as dots
    std::size_t typed = 0; // characters of a secret answer
    bool answerSelected = false; // all of the answer, as Ctrl+A selects it
    std::string text; // what is typed of an answer that may be seen
    bool checking = false; // what was typed is with PAM
    unsigned pulse = 0;
    bool wrong = false;
    bool focused = false;
    bool caret = true;
    bool capsLock = false;
    struct Line {
        std::string text;
        bool error = false;
    };
    std::vector<Line> messages; // what PAM said, and what went wrong

    std::vector<std::string> sessions; // to choose from, when there are several
    int session = 0; // the one chosen
    bool sessionsOpen = false;
    std::string keyboardLayout; // shown when there are several to switch between

    std::vector<std::string> powerActions;
    bool powerOpen = false;
    int menuSelected = -1; // the item of the open menu that the keys are on

    std::string notice; // at the bottom, about how the login screen is set up
};

// Draws `screen` on an output of `width` by `height`, in its logical size: all of it on the
// `main` output, the background alone on the others. What can be clicked on the main one goes
// to `targets`, the topmost first.
void draw(cairo_t* cr, int width, int height, const Screen& screen, bool main, std::vector<Target>* targets);

} // namespace charon
