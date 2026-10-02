#include "Server.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <signal.h>

namespace {

void usage(const char* program)
{
    std::printf("Usage: %s [--debug] [--version]\n\n"
                "Atlas, the compositor of TDE. Runs ~/.config/tde/autostart, or else the one TDE comes with,\n"
                "once it is up.\n",
        program);
}

std::string autostartFile()
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::filesystem::path dir = config && *config ? config : std::filesystem::path(home ? home : "") / ".config";
    const std::filesystem::path own = dir / "tde" / "autostart";
    return std::filesystem::exists(own) ? own.string() : std::string(TDE_DATA_DIR "/autostart");
}

int terminate(int, void* data)
{
    static_cast<atlas::Server*>(data)->terminate();
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) {
            debug = true;
        } else if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("tde-atlas %s\n", TDE_SESSION_VERSION);
            return 0;
        } else {
            usage(argv[0]);
            return std::strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }
    wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, nullptr);

    atlas::Server server;
    if (!server.start())
        return 1;
    wl_event_loop_add_signal(server.eventLoop, SIGTERM, terminate, &server);
    wl_event_loop_add_signal(server.eventLoop, SIGINT, terminate, &server);

    // Programs started through D-Bus or systemd need to find the display too. Then the
    // session's target starts, and with it the programs of the XDG autostart folders, which
    // systemd makes services of; they look at XDG_CURRENT_DESKTOP, so it has to be there first,
    // and those added since systemd last looked are found by reloading it.
    setenv("XDG_CURRENT_DESKTOP", "TDE", false);
    setenv("XDG_SESSION_TYPE", "wayland", true);
    atlas::Server::spawn("dbus-update-activation-environment --systemd WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP "
                         "XDG_SESSION_TYPE XDG_SESSION_DESKTOP && systemctl --user daemon-reload && "
                         "systemctl --user start tde-session.target");
    // The path as an argument of its own, whatever characters it has.
    atlas::Server::spawn("exec /bin/sh \"$1\"", autostartFile());

    wlr_log(WLR_INFO, "running on WAYLAND_DISPLAY=%s", server.socketName().c_str());
    server.run();

    // The programs that came with the session go with it, and nothing started from now on
    // looks for this display.
    [[maybe_unused]] const int stopped = std::system("systemctl --user stop tde-session.target; "
                                                     "systemctl --user unset-environment WAYLAND_DISPLAY DISPLAY "
                                                     "XDG_SESSION_TYPE");
    return 0;
}
