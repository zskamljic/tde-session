#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atlas {

// Where an application's window was last: on which display, at what place in it and how large,
// and in which tile, if any.
struct RememberedWindow {
    std::string appId;
    std::string display; // as Settings knows displays, by make, model and serial
    int x = 0; // of the window and its title bar, from the display's top left corner
    int y = 0;
    int width = 0; // of the program's own part, without the title bar
    int height = 0;
    int tile = 0; // as atlas::Tile has it; the place and size are where it goes back to

    bool operator==(const RememberedWindow&) const = default;
};

// The windows of applications as they were left, so the next one shows where the last one was.
// Kept in ~/.local/state/tde/windows, a line each, the newest last.
class WindowMemory {
public:
    explicit WindowMemory(std::string path = defaultPath());

    static std::string defaultPath();

    const RememberedWindow* find(std::string_view appId) const;
    // Keeps `window` for its application, in place of what was kept, and writes it down.
    void remember(RememberedWindow window);

    // The lines of the file, and back.
    static std::vector<RememberedWindow> parse(std::string_view text);
    static std::string format(const std::vector<RememberedWindow>& windows);

private:
    void save() const;

    std::string m_path;
    std::vector<RememberedWindow> m_windows;
};

} // namespace atlas
