#include "Greeter.hpp"
#include "Sessions.hpp"

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// The language of the system, for the clock, when the greeter's environment has none.
void systemLanguage()
{
    if (std::getenv("LANG"))
        return;
    std::ifstream config("/etc/locale.conf");
    std::string line;
    while (std::getline(config, line)) {
        if (!line.starts_with("LANG="))
            continue;
        std::string value = line.substr(5);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        setenv("LANG", value.c_str(), true);
    }
}

// Copies the file at `from`, which must be `owner`'s own and no link, to `to`, owned by `uid`
// and `gid`. Whoever runs this as root should not be made to publish a file of someone else's.
bool copyOwn(const std::filesystem::path& from, const std::filesystem::path& to, uid_t owner, uid_t uid, gid_t gid)
{
    const int in = open(from.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) {
        std::printf("  %s: not there, left as it was\n", from.c_str());
        return true;
    }
    struct stat info {};
    if (fstat(in, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != owner || info.st_size > (1 << 20)) {
        std::fprintf(stderr, "tde-charon: %s is no file of the user's own\n", from.c_str());
        close(in);
        return false;
    }
    std::string text(std::size_t(info.st_size), '\0');
    const bool read = ::read(in, text.data(), text.size()) == ssize_t(text.size());
    close(in);
    std::error_code error;
    std::filesystem::create_directories(to.parent_path(), error);
    for (auto folder = to.parent_path(); folder.string().size() > std::strlen(TDE_GREETER_DIR);
        folder = folder.parent_path()) {
        [[maybe_unused]] const int changed = chown(folder.c_str(), uid, gid);
    }
    std::ofstream out(to, std::ios::trunc);
    out << text;
    if (!read || !out.flush()) {
        std::fprintf(stderr, "tde-charon: cannot copy %s to %s\n", from.c_str(), to.c_str());
        return false;
    }
    out.close();
    [[maybe_unused]] const int changed = chown(to.c_str(), uid, gid);
    chmod(to.c_str(), 0644);
    std::printf("  %s\n", to.c_str());
    return true;
}

// Gives the login screen `name`'s settings of TDE: the displays as they laid them out, and the
// pointer, touchpad and keyboard as they have them.
int takeSettings(const char* name)
{
    if (geteuid() != 0) {
        std::fprintf(stderr, "tde-charon: run this as root, as with sudo tde-charon --take-settings\n");
        return 1;
    }
    if (!name) {
        name = std::getenv("SUDO_USER");
        if (const char* uid = std::getenv("PKEXEC_UID"); !name && uid) {
            if (const passwd* entry = getpwuid(uid_t(std::strtoul(uid, nullptr, 10))))
                name = entry->pw_name;
        }
    }
    const passwd* user = name ? getpwnam(name) : nullptr;
    if (!user) {
        std::fprintf(stderr, "tde-charon: whose settings? Name the user: tde-charon --take-settings USER\n");
        return 1;
    }
    const uid_t owner = user->pw_uid;
    const std::filesystem::path home = user->pw_dir;
    // Owned by the login screen's user, as its folder is.
    struct stat folder {};
    if (stat(TDE_GREETER_DIR, &folder) != 0) {
        std::fprintf(stderr, "tde-charon: %s is not there; is tde-session installed?\n", TDE_GREETER_DIR);
        return 1;
    }
    std::printf("The login screen takes %s's settings:\n", user->pw_name);
    const std::filesystem::path config = std::filesystem::path(TDE_GREETER_DIR) / "config/tde";
    const bool copied = copyOwn(home / ".config/tde/session/config.lua", config / "session/config.lua", owner,
                            folder.st_uid, folder.st_gid)
        && copyOwn(home / ".config/tde/config.lua", config / "config.lua", owner, folder.st_uid, folder.st_gid);
    return copied ? 0 : 1;
}

// The first client of an X session that startx started: the session's own command, which
// waited in the environment so that startx would not split it, runs in its place.
int runX11Session()
{
    const char* command = std::getenv(charon::x11SessionVariable);
    if (!command || !*command) {
        std::fprintf(stderr, "tde-charon: --x11-session is for X sessions the login screen starts\n");
        return 1;
    }
    const std::string line = std::string("exec ") + command;
    unsetenv(charon::x11SessionVariable);
    execl("/bin/sh", "sh", "-c", line.c_str(), static_cast<char*>(nullptr));
    std::perror("tde-charon: /bin/sh");
    return 127;
}

} // namespace

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("tde-charon " TDE_SESSION_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--x11-session") == 0)
            return runX11Session();
        if (std::strcmp(argv[i], "--take-settings") == 0)
            return takeSettings(i + 1 < argc ? argv[i + 1] : nullptr);
        std::puts("Usage: tde-charon [--take-settings [USER]]\n\n"
                  "Charon, the login screen of TDE, for greetd; it runs in the compositor, as\n"
                  "  tde-atlas --greeter tde-charon\n\n"
                  "With --take-settings, run as root, gives the login screen the TDE settings of USER, or of\n"
                  "whoever ran sudo: how the displays are laid out, and the pointer, touchpad and keyboard.");
        return std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0 ? 0 : 1;
    }
    systemLanguage();
    std::setlocale(LC_ALL, "");
    return charon::greet();
}
