#include "Users.hpp"

#include <Owned.hpp>

#include <pwd.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace charon {
namespace {

using Bus = cerberus::Owned<sd_bus, sd_bus_flush_close_unref>;
using Message = cerberus::Owned<sd_bus_message, sd_bus_message_unref>;

constexpr const char* Accounts = "org.freedesktop.Accounts";

std::string stringProperty(sd_bus* bus, const char* path, const char* name)
{
    char* value = nullptr;
    if (sd_bus_get_property_string(bus, Accounts, path, "org.freedesktop.Accounts.User", name, nullptr, &value) < 0)
        return {};
    std::string out = value ? value : "";
    std::free(value);
    return out;
}

bool boolProperty(sd_bus* bus, const char* path, const char* name)
{
    int value = 0;
    if (sd_bus_get_property_trivial(bus, Accounts, path, "org.freedesktop.Accounts.User", name, nullptr, 'b', &value)
        < 0)
        return false;
    return value != 0;
}

// The range of UIDs for people, as login.defs has it.
std::pair<uid_t, uid_t> peopleUids()
{
    uid_t min = 1000;
    uid_t max = 60000;
    std::ifstream defs("/etc/login.defs");
    std::string key;
    unsigned long value = 0;
    std::string line;
    while (std::getline(defs, line)) {
        std::istringstream words(line);
        if (!(words >> key >> value))
            continue;
        if (key == "UID_MIN")
            min = uid_t(value);
        else if (key == "UID_MAX")
            max = uid_t(value);
    }
    return {min, max};
}

User fromPasswd(const passwd& entry)
{
    User user;
    user.name = entry.pw_name;
    user.realName = realNameOf(entry.pw_gecos ? entry.pw_gecos : "");
    if (user.realName.empty())
        user.realName = user.name;
    user.shell = entry.pw_shell ? entry.pw_shell : "";
    return user;
}

std::vector<User> fromAccountsService()
{
    sd_bus* opened = nullptr;
    if (sd_bus_open_system(&opened) < 0)
        return {};
    const Bus bus(opened);
    sd_bus_message* replied = nullptr;
    if (sd_bus_call_method(bus.get(), Accounts, "/org/freedesktop/Accounts", "org.freedesktop.Accounts",
            "ListCachedUsers", nullptr, &replied, "")
        < 0)
        return {};
    const Message reply(replied);
    std::vector<std::string> paths;
    if (sd_bus_message_enter_container(reply.get(), 'a', "o") < 0)
        return {};
    const char* path = nullptr;
    while (sd_bus_message_read(reply.get(), "o", &path) > 0)
        paths.emplace_back(path);

    std::vector<User> users;
    for (const std::string& userPath : paths) {
        if (boolProperty(bus.get(), userPath.c_str(), "SystemAccount")
            || boolProperty(bus.get(), userPath.c_str(), "Locked"))
            continue;
        const std::string name = stringProperty(bus.get(), userPath.c_str(), "UserName");
        const passwd* entry = name.empty() ? nullptr : getpwnam(name.c_str());
        if (!entry)
            continue;
        User user = fromPasswd(*entry);
        if (std::string real = stringProperty(bus.get(), userPath.c_str(), "RealName"); !real.empty())
            user.realName = std::move(real);
        if (std::string icon = stringProperty(bus.get(), userPath.c_str(), "IconFile");
            !icon.empty() && access(icon.c_str(), R_OK) == 0)
            user.icon = std::move(icon);
        user.session = stringProperty(bus.get(), userPath.c_str(), "Session");
        if (user.session.empty())
            user.session = stringProperty(bus.get(), userPath.c_str(), "XSession");
        users.push_back(std::move(user));
    }
    return users;
}

} // namespace

std::string realNameOf(std::string_view gecos)
{
    std::string_view name = gecos.substr(0, gecos.find(','));
    while (!name.empty() && name.back() == ' ')
        name.remove_suffix(1);
    return std::string(name);
}

bool loginShell(std::string_view shell)
{
    return !shell.empty() && !shell.ends_with("/nologin") && !shell.ends_with("/false");
}

std::vector<User> findUsers()
{
    std::vector<User> users = fromAccountsService();
    if (users.empty()) {
        const auto [min, max] = peopleUids();
        setpwent();
        while (const passwd* entry = getpwent()) {
            if (entry->pw_uid >= min && entry->pw_uid <= max && loginShell(entry->pw_shell ? entry->pw_shell : ""))
                users.push_back(fromPasswd(*entry));
        }
        endpwent();
    }
    std::ranges::sort(users, [](const User& a, const User& b) { return a.realName < b.realName; });
    return users;
}

User findUser(const std::string& name)
{
    if (const passwd* entry = getpwnam(name.c_str()))
        return fromPasswd(*entry);
    // PAM may know them, as from a directory; it decides.
    User user;
    user.name = name;
    user.realName = name;
    return user;
}

Memory::Memory(std::string path)
    : m_path(std::move(path))
{
    std::ifstream file(m_path);
    const std::string text {std::istreambuf_iterator<char>(file), {}};
    Memory parsed = parse(text);
    m_lastUser = std::move(parsed.m_lastUser);
    m_sessions = std::move(parsed.m_sessions);
}

Memory Memory::parse(std::string_view text)
{
    Memory memory;
    std::istringstream lines {std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string key;
        std::string user;
        std::string session;
        words >> key >> user;
        if (key == "last" && !user.empty())
            memory.m_lastUser = user;
        else if (key == "session" && words >> session)
            memory.m_sessions.emplace_back(user, session);
    }
    return memory;
}

std::string Memory::text() const
{
    std::string text;
    if (!m_lastUser.empty())
        text += "last " + m_lastUser + '\n';
    for (const auto& [user, session] : m_sessions)
        text += "session " + user + ' ' + session + '\n';
    return text;
}

std::string Memory::sessionOf(std::string_view user) const
{
    const auto it = std::ranges::find(m_sessions, user, &std::pair<std::string, std::string>::first);
    return it == m_sessions.end() ? std::string() : it->second;
}

void Memory::loggedIn(const std::string& user, const std::string& session)
{
    // Names with spaces could not be read back; they are rare enough to forget.
    if (user.empty() || user.contains(' ') || user.contains('\n'))
        return;
    m_lastUser = user;
    std::erase_if(m_sessions, [&](const auto& entry) { return entry.first == user; });
    if (!session.empty() && !session.contains(' ') && !session.contains('\n'))
        m_sessions.emplace_back(user, session);
    if (m_path.empty())
        return;
    // Written whole and moved into place, so that it is never found half written.
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_path).parent_path(), error);
    const std::string written = m_path + ".new";
    {
        std::ofstream file(written, std::ios::trunc);
        file << text();
        if (!file.flush()) {
            std::fprintf(stderr, "tde-charon: cannot remember who logged in, in %s\n", m_path.c_str());
            return;
        }
    }
    std::filesystem::rename(written, m_path, error);
}

} // namespace charon
