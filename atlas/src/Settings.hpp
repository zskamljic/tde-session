#pragma once

#include <string>
#include <vector>

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

// What pushing the pointer into the top left corner, or clicking the bar's button there, does.
enum class HotCorner { Overview, Desktop, Off };

// What the compositor takes from the session's settings, ~/.config/tde/session/config.lua,
// which Settings (Daedalus) writes.
struct Settings {
    PointerSettings mouse;
    PointerSettings touchpad {.naturalScroll = true};
    int windowAnimation = 200; // ms windows take to move into a tile
    HotCorner hotCorner = HotCorner::Overview; // the overview, as Super opens it
    std::string primaryDisplay; // as Settings knows displays, by make, model and serial; empty for the first

    bool operator==(const Settings&) const = default;
};

enum class WindowButton { Minimize, Maximize, Close };

// What the compositor takes from the desktop's config, ~/.config/tde/config.lua, which all of
// TDE's programs follow: the title bars it draws look like theirs, and the pointer is the same.
struct DesktopSettings {
    bool buttonsLeft = false; // the window buttons at the left end of title bars
    std::vector<WindowButton> buttons {WindowButton::Minimize, WindowButton::Maximize, WindowButton::Close};
    std::string cursorTheme; // empty for the one XCURSOR_THEME names, or the default
    int cursorSize = 0; // 0 for the one XCURSOR_SIZE says, or 24

    bool operator==(const DesktopSettings&) const = default;
};

std::string settingsPath();
// What is missing or wrong keeps its default; problems go to the log.
Settings loadSettings(const std::string& path);

std::string desktopSettingsPath();
DesktopSettings loadDesktopSettings(const std::string& path);

} // namespace atlas
