#pragma once

#include <string>

namespace atlas {

// How a kind of pointing device behaves.
struct PointerSettings {
    int speed = 0; // of the pointer, -100 to 100 percent around what libinput does
    int scrollSpeed = 100; // percent of the distance scrolled
    bool naturalScroll = false; // the content follows the fingers, or the wheel the same way
    bool tapToClick = true; // touchpads only
    bool disableWhileTyping = true; // touchpads only

    bool operator==(const PointerSettings&) const = default;
};

// What the compositor takes from the session's settings, ~/.config/tde/session/config.lua,
// which Settings (Daedalus) writes.
struct Settings {
    PointerSettings mouse;
    PointerSettings touchpad {.naturalScroll = true};
    int windowAnimation = 200; // ms windows take to move into a tile

    bool operator==(const Settings&) const = default;
};

std::string settingsPath();
// What is missing or wrong keeps its default; problems go to the log.
Settings loadSettings(const std::string& path);

} // namespace atlas
