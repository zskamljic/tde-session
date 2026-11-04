#include "Settings.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace atlas;

namespace {

int failures = 0;

void expect(bool condition, const char* what)
{
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

std::string write(const std::string& name, const std::string& text)
{
    const std::string path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream(path) << text;
    return path;
}

} // namespace

int main()
{
    const std::string layouts = write("tst_settings_layouts.lua", R"lua(return {
        displays = {
            primary = "Dell U2720Q 1234",
            layouts = {
                { { display = "BOE 0x0BCA (eDP-1)", width = 2256, height = 1504, refresh = 59999, scale = 150 } },
                { { display = "BOE 0x0BCA (eDP-1)", enabled = false },
                  { display = "Dell U2720Q 1234", width = 3840, height = 2160, x = 0, y = 0, scale = 175, transform = 1 } },
                { { width = 10 } },
            },
        },
    })lua");
    const Settings settings = loadSettings(layouts);
    expect(settings.primaryDisplay == "Dell U2720Q 1234", "reads the primary display");
    expect(settings.displayLayouts.size() == 2, "reads each layout, leaving out one naming no display");
    if (settings.displayLayouts.size() == 2) {
        const DisplayPlacement& laptop = settings.displayLayouts[0][0];
        expect(laptop.width == 2256 && laptop.height == 1504 && laptop.refresh == 59999 && laptop.scale == 150,
            "reads a display's mode and scale");
        expect(laptop.enabled && laptop.transform == 0, "takes a display without them as on and upright");
        const DisplayLayout& docked = settings.displayLayouts[1];
        expect(docked.size() == 2 && !docked[0].enabled, "reads a display turned off");
        expect(docked.size() == 2 && docked[1].scale == 175 && docked[1].transform == 1, "reads how it is turned");
    }

    // At the login screen, settings copied from a session are values, never programs.
    const std::string reaching = write("tst_settings_reaching.lua",
        R"(return { windows = { corner = os and "off" or "desktop" }, input = { keyboard = { numlock = io and "on" or "off" } } })");
    expect(loadSettings(reaching).hotCorner == HotCorner::Off, "has os in a session");
    restrictSettings();
    const Settings restricted = loadSettings(reaching);
    expect(restricted.hotCorner == HotCorner::Desktop && restricted.numLock == NumLock::Off,
        "has neither os nor io at the login screen");
    const std::string loading = write("tst_settings_loading.lua",
        R"(return { windows = { corner = (load or dofile or loadfile or require) and "off" or "desktop" } })");
    expect(loadSettings(loading).hotCorner == HotCorner::Desktop, "cannot load code at the login screen");
    expect(loadSettings(layouts).displayLayouts.size() == 2, "still reads values at the login screen");

    for (const auto& path : {layouts, reaching, loading})
        std::filesystem::remove(path);
    if (failures == 0)
        std::puts("all passed");
    return failures == 0 ? 0 : 1;
}
