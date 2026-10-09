#include "Settings.hpp"

#include "wlr.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>

namespace atlas {
namespace {

// The value at `key` of the table on top of the stack, when it is of the kind asked for.
std::optional<int> integer(lua_State* lua, const char* key, int min, int max)
{
    lua_getfield(lua, -1, key);
    std::optional<int> value;
    int isInteger = 0;
    const lua_Integer number = lua_tointegerx(lua, -1, &isInteger);
    if (isInteger)
        value = int(std::clamp<lua_Integer>(number, min, max));
    lua_pop(lua, 1);
    return value;
}

std::optional<bool> boolean(lua_State* lua, const char* key)
{
    lua_getfield(lua, -1, key);
    std::optional<bool> value;
    if (lua_isboolean(lua, -1))
        value = lua_toboolean(lua, -1) != 0;
    lua_pop(lua, 1);
    return value;
}

void table(lua_State* lua, const char* key, const std::function<void()>& body)
{
    lua_getfield(lua, -1, key);
    if (lua_istable(lua, -1))
        body();
    lua_pop(lua, 1);
}

void readPointer(lua_State* lua, PointerSettings& pointer)
{
    if (const auto speed = integer(lua, "speed", -100, 100))
        pointer.speed = *speed;
    if (const auto scroll = integer(lua, "scroll_speed", 10, 1000))
        pointer.scrollSpeed = *scroll;
    if (const auto natural = boolean(lua, "natural_scroll"))
        pointer.naturalScroll = *natural;
    if (const auto tap = boolean(lua, "tap_to_click"))
        pointer.tapToClick = *tap;
    if (const auto typing = boolean(lua, "disable_while_typing"))
        pointer.disableWhileTyping = *typing;
}

} // namespace

std::string settingsPath()
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string folder = config && *config ? config : std::string(home ? home : "") + "/.config";
    return folder + "/tde/session/config.lua";
}

Settings loadSettings(const std::string& path)
{
    Settings settings;
    const std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    lua_State* lua = state.get();
    if (!lua)
        return settings;
    luaL_openlibs(lua);
    if (luaL_loadfile(lua, path.c_str()) != LUA_OK || lua_pcall(lua, 0, 1, 0) != LUA_OK) {
        // No file is no problem: it has the defaults.
        if (const char* error = lua_tostring(lua, -1); error && !std::string_view(error).contains("No such file"))
            wlr_log(WLR_ERROR, "%s", error);
        return settings;
    }
    if (!lua_istable(lua, -1))
        return settings;
    table(lua, "input", [&] {
        table(lua, "mouse", [&] { readPointer(lua, settings.mouse); });
        table(lua, "touchpad", [&] { readPointer(lua, settings.touchpad); });
    });
    table(lua, "animations", [&] {
        if (const auto windows = integer(lua, "windows", 0, 2000))
            settings.windowAnimation = *windows;
    });
    return settings;
}

} // namespace atlas
