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
    // Qt and GTK 3 programs open and save files through xdg-desktop-portal, so in Ariadne,
    // unless told otherwise.
    setenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal", false);
    setenv("GTK_USE_PORTAL", "1", false);
    // Passwords and passphrases for ssh, git and `sudo -A` are asked for in a window, and keys are
    // unlocked once a session, in openssh's agent, unless the session has an agent of its own.
    setenv("SSH_ASKPASS", TDE_ASKPASS, false);
    setenv("SSH_ASKPASS_REQUIRE", "prefer", false);
    setenv("SUDO_ASKPASS", TDE_ASKPASS, false);
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    const bool ownAgent = !std::getenv("SSH_AUTH_SOCK") && runtime;
    if (ownAgent)
        setenv("SSH_AUTH_SOCK", (std::string(runtime) + "/ssh-agent.socket").c_str(), true);
    atlas::Server::spawn(std::string("dbus-update-activation-environment --systemd WAYLAND_DISPLAY DISPLAY "
                                     "XDG_CURRENT_DESKTOP XDG_SESSION_TYPE XDG_SESSION_DESKTOP QT_QPA_PLATFORMTHEME "
                                     "GTK_USE_PORTAL SSH_ASKPASS SSH_ASKPASS_REQUIRE SUDO_ASKPASS SSH_AUTH_SOCK && "
                                     "systemctl --user daemon-reload && ")
        + (ownAgent ? "systemctl --user start ssh-agent.socket; " : "") + "systemctl --user start tde-session.target");
    // The path as an argument of its own, whatever characters it has.
    atlas::Server::spawn("exec /bin/sh \"$1\"", autostartFile());

    wlr_log(WLR_INFO, "running on WAYLAND_DISPLAY=%s", server.socketName().c_str());
    server.run();

    // The programs that came with the session go with it, and nothing started from now on
    // looks for this display.
    [[maybe_unused]] const int stopped = std::system("systemctl --user stop tde-session.target; "
                                                     "systemctl --user unset-environment WAYLAND_DISPLAY DISPLAY "
                                                     "XDG_SESSION_TYPE QT_QPA_PLATFORMTHEME GTK_USE_PORTAL "
                                                     "SSH_ASKPASS SSH_ASKPASS_REQUIRE SUDO_ASKPASS");
    return 0;
}
