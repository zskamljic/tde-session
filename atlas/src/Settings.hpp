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

// How Num Lock is at login: as it was left, or on or off whatever it was.
enum class NumLock { Last, On, Off };

// Where a display goes in a layout Settings saved: known by make, model and serial number, as
// for the primary display, with its mode, its scale in percent and how it is turned.
struct DisplayPlacement {
    std::string display;
    bool enabled = true;
    int width = 0;
    int height = 0;
    int refresh = 0; // mHz
    int x = 0;
    int y = 0;
    int scale = 100;
    int transform = 0; // as wl_output has it

    bool operator==(const DisplayPlacement&) const = default;
};

// The displays plugged in together, and where each goes.
using DisplayLayout = std::vector<DisplayPlacement>;

// What the compositor takes from the session's settings, ~/.config/tde/session/config.lua,
// which Settings (Daedalus) writes.
struct Settings {
    NumLock numLock = NumLock::Last;
    PointerSettings mouse;
    PointerSettings touchpad {.naturalScroll = true};
    int windowAnimation = 200; // ms windows take to move into a tile
    HotCorner hotCorner = HotCorner::Overview; // the overview, as Super opens it
    std::string primaryDisplay; // as Settings knows displays, by make, model and serial; empty for the first
    // Laid out by the bar in a session; at the login screen, by the compositor itself.
    std::vector<DisplayLayout> displayLayouts;

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
    bool compactTitleBars = false; // lower title bars, with smaller buttons

    bool operator==(const DesktopSettings&) const = default;
};

// From now on, settings files are read without the libraries that reach out of Lua: at the login
// screen they were copied from someone's session, and only their values are wanted.
void restrictSettings();

std::string settingsPath();
// What is missing or wrong keeps its default; problems go to the log.
Settings loadSettings(const std::string& path);

std::string desktopSettingsPath();
DesktopSettings loadDesktopSettings(const std::string& path);

// Whether Num Lock is on, kept in ~/.local/state/tde/numlock for the next login, and written
// there by Settings to turn it on or off on keyboards without the key.
std::string numLockStatePath();
bool loadNumLockState(const std::string& path);
void saveNumLockState(const std::string& path, bool on);

} // namespace atlas
