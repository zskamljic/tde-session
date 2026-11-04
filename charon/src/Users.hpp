#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace charon {

// Someone who can log in here.
struct User {
    std::string name; // to log in as
    std::string realName; // to show; the login name when there is none
    std::string icon; // a picture of them, when they chose one
    std::string session; // the session AccountsService remembers for them, as GDM leaves it
    std::string shell;
};

// The people AccountsService knows, as GDM lists them, without system accounts; when it is not
// there, those of /etc/passwd with a UID for people and a shell to log in with. By name.
std::vector<User> findUsers();
// One not listed, by the name typed.
User findUser(const std::string& name);

// The real name of the GECOS field: what comes before the first comma.
std::string realNameOf(std::string_view gecos);
// Whether `shell` is one to log in with, rather than nologin or false.
bool loginShell(std::string_view shell);

// What the login screen remembers between logins: who logged in last, and which session each
// one chose.
class Memory {
public:
    explicit Memory(std::string path);

    const std::string& lastUser() const { return m_lastUser; }
    std::string sessionOf(std::string_view user) const;
    // Kept for the next time, written at once.
    void loggedIn(const std::string& user, const std::string& session);

    // The file's lines, as written; for tests.
    static Memory parse(std::string_view text);
    std::string text() const;

private:
    Memory() = default;

    std::string m_path;
    std::string m_lastUser;
    std::vector<std::pair<std::string, std::string>> m_sessions; // user and session id
};

} // namespace charon
