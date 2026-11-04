#include "Sessions.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace charon {
namespace {

std::string trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r");
    return std::string(text.substr(first, last - first + 1));
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find(separator, start), text.size());
        if (std::string part = trimmed(text.substr(start, end - start)); !part.empty())
            parts.push_back(std::move(part));
        start = end + 1;
    }
    return parts;
}

// Whether `program` can run: a path that can be executed, or a name found on PATH.
bool runnable(const std::string& program)
{
    if (program.empty())
        return false;
    if (program.contains('/'))
        return access(program.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    for (const std::string& folder : split(path && *path ? path : "/usr/local/bin:/usr/bin", ':')) {
        if (access((folder + '/' + program).c_str(), X_OK) == 0)
            return true;
    }
    return false;
}


// The command line of Exec without the field codes, such as %f, that only launchers fill in.
std::string withoutFieldCodes(const std::string& exec)
{
    std::string out;
    for (std::size_t i = 0; i < exec.size(); ++i) {
        if (exec[i] == '%' && i + 1 < exec.size()) {
            if (exec[i + 1] == '%')
                out += '%';
            ++i;
            continue;
        }
        out += exec[i];
    }
    return trimmed(out);
}

} // namespace

bool parseSession(std::string_view text, std::string_view language, Session& session)
{
    // The keys of the main group, with their translations.
    std::map<std::string, std::string> keys;
    bool inEntry = false;
    std::istringstream lines {std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        const std::string current = trimmed(line);
        if (current.empty() || current.front() == '#')
            continue;
        if (current.front() == '[') {
            inEntry = current == "[Desktop Entry]";
            continue;
        }
        const auto equals = current.find('=');
        if (inEntry && equals != std::string::npos)
            keys.try_emplace(trimmed(std::string_view(current).substr(0, equals)),
                trimmed(std::string_view(current).substr(equals + 1)));
    }
    const auto value = [&](const std::string& key) {
        const auto it = keys.find(key);
        return it == keys.end() ? std::string() : it->second;
    };
    if (value("Hidden") == "true" || value("NoDisplay") == "true" || value("Exec").empty())
        return false;
    if (const std::string tryExec = value("TryExec"); !tryExec.empty() && !runnable(tryExec))
        return false;

    // Name[sl_SI], then Name[sl], then Name, for a language of sl_SI.UTF-8.
    std::string wanted(language.substr(0, language.find_first_of(".@")));
    session.name.clear();
    while (!wanted.empty() && wanted != "C" && wanted != "POSIX") {
        if (const std::string name = value("Name[" + wanted + ']'); !name.empty()) {
            session.name = name;
            break;
        }
        const auto underscore = wanted.find('_');
        wanted = underscore == std::string::npos ? std::string() : wanted.substr(0, underscore);
    }
    if (session.name.empty())
        session.name = value("Name");
    session.exec = withoutFieldCodes(value("Exec"));
    session.desktopNames = split(value("DesktopNames"), ';');
    if (session.name.empty())
        session.name = session.id;
    return !session.exec.empty();
}

std::vector<Session> findSessions(bool x11)
{
    const char* dataDirs = std::getenv("XDG_DATA_DIRS");
    const std::vector<std::string> folders
        = split(dataDirs && *dataDirs ? dataDirs : "/usr/local/share:/usr/share", ':');
    const char* lang = std::getenv("LANG");
    std::vector<Session> sessions;
    for (const bool x : {false, true}) {
        if (x && !x11)
            continue;
        for (const std::string& folder : folders) {
            std::error_code error;
            for (const auto& file :
                std::filesystem::directory_iterator(folder + (x ? "/xsessions" : "/wayland-sessions"), error)) {
                if (file.path().extension() != ".desktop")
                    continue;
                Session session;
                session.id = file.path().stem().string();
                session.x11 = x;
                // The first of an id hides the others, Wayland's those of X11.
                if (std::ranges::contains(sessions, session.id, &Session::id))
                    continue;
                std::ifstream stream(file.path());
                const std::string text {std::istreambuf_iterator<char>(stream), {}};
                if (parseSession(text, lang ? lang : "", session))
                    sessions.push_back(std::move(session));
            }
        }
    }
    // By name as people read them, "labwc" among the L's and not after every capital.
    std::ranges::stable_sort(sessions, [](const Session& a, const Session& b) {
        return std::ranges::lexicographical_compare(a.name, b.name, [](unsigned char x, unsigned char y) {
            return std::tolower(x) < std::tolower(y);
        });
    });
    return sessions;
}

std::vector<std::string> sessionCommand(const Session& session, const std::string& shell)
{
    // An X session needs the server started for it; startx does that on the terminal greetd
    // gives the session.
    const std::string command
        = session.x11 ? "exec startx " TDE_CHARON_PATH " --x11-session" : "exec " + session.exec;
    return {shell.empty() ? std::string("/bin/sh") : shell, "-l", "-c", command};
}

std::vector<std::string> sessionEnvironment(const Session& session)
{
    std::vector<std::string> environment {
        "XDG_SESSION_TYPE=" + std::string(session.x11 ? "x11" : "wayland"),
        "XDG_SESSION_DESKTOP=" + session.id,
        "DESKTOP_SESSION=" + session.id,
    };
    if (!session.desktopNames.empty()) {
        std::string desktop;
        for (const std::string& name : session.desktopNames)
            desktop += (desktop.empty() ? "" : ":") + name;
        environment.push_back("XDG_CURRENT_DESKTOP=" + desktop);
    }
    if (session.x11)
        environment.push_back(std::string(x11SessionVariable) + '=' + session.exec);
    return environment;
}

std::string shellQuoted(const std::string& text)
{
    std::string out = "'";
    for (const char c : text)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + '\'';
}

std::string commandLine(const std::vector<std::string>& command)
{
    std::string line;
    for (const std::string& word : command)
        line += (line.empty() ? "" : " ") + shellQuoted(word);
    return line;
}

} // namespace charon
