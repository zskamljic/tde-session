#include "WindowMemory.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace atlas {
namespace {

// Applications forgotten after this many others were remembered since.
constexpr std::size_t MaxRemembered = 200;
constexpr int MaxPixels = 100'000;

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos)
            return parts;
        start = end + 1;
    }
}

std::optional<int> number(std::string_view text, int low, int high)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() || value < low || value > high)
        return std::nullopt;
    return value;
}

bool plain(std::string_view text)
{
    return !text.empty() && text.find_first_of("\t\n") == std::string_view::npos;
}

} // namespace

WindowMemory::WindowMemory(std::string path)
    : m_path(std::move(path))
{
    std::ifstream file(m_path);
    std::stringstream text;
    text << file.rdbuf();
    m_windows = parse(text.str());
}

std::string WindowMemory::defaultPath()
{
    const char* state = std::getenv("XDG_STATE_HOME");
    const char* home = std::getenv("HOME");
    return (state && *state ? state : std::string(home ? home : "") + "/.local/state") + "/tde/windows";
}

const RememberedWindow* WindowMemory::find(std::string_view appId) const
{
    const auto it = std::ranges::find(m_windows, appId, &RememberedWindow::appId);
    return it == m_windows.end() ? nullptr : &*it;
}

void WindowMemory::remember(RememberedWindow window)
{
    if (!plain(window.appId) || !plain(window.display) || window.width <= 0 || window.height <= 0)
        return;
    if (const RememberedWindow* kept = find(window.appId); kept && *kept == window)
        return;
    std::erase_if(m_windows, [&](const RememberedWindow& other) { return other.appId == window.appId; });
    m_windows.push_back(std::move(window));
    if (m_windows.size() > MaxRemembered)
        m_windows.erase(m_windows.begin(), m_windows.end() - MaxRemembered);
    save();
}

std::vector<RememberedWindow> WindowMemory::parse(std::string_view text)
{
    std::vector<RememberedWindow> windows;
    for (const std::string_view line : split(text, '\n')) {
        const auto fields = split(line, '\t');
        if (fields.size() != 7 || !plain(fields[0]) || !plain(fields[1]))
            continue;
        const auto x = number(fields[2], -MaxPixels, MaxPixels);
        const auto y = number(fields[3], -MaxPixels, MaxPixels);
        const auto width = number(fields[4], 1, MaxPixels);
        const auto height = number(fields[5], 1, MaxPixels);
        const auto tile = number(fields[6], 0, 7);
        if (!x || !y || !width || !height || !tile)
            continue;
        std::erase_if(windows, [&](const RememberedWindow& other) { return other.appId == fields[0]; });
        windows.push_back({std::string(fields[0]), std::string(fields[1]), *x, *y, *width, *height, *tile});
    }
    return windows;
}

std::string WindowMemory::format(const std::vector<RememberedWindow>& windows)
{
    std::string text;
    for (const RememberedWindow& w : windows) {
        text += w.appId + '\t' + w.display + '\t' + std::to_string(w.x) + '\t' + std::to_string(w.y) + '\t'
            + std::to_string(w.width) + '\t' + std::to_string(w.height) + '\t' + std::to_string(w.tile) + '\n';
    }
    return text;
}

void WindowMemory::save() const
{
    // Written whole and moved into place, so a crash never leaves half of it.
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_path).parent_path(), error);
    const std::string written = m_path + ".new";
    {
        std::ofstream file(written, std::ios::trunc);
        file << format(m_windows);
        if (!file.flush())
            return;
    }
    std::filesystem::rename(written, m_path, error);
}

} // namespace atlas
