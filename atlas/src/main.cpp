#include "Server.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void usage(const char* program)
{
    std::printf("Usage: %s [--debug] [--version] [--greeter COMMAND [ARGUMENT...]]\n\n"
                "Atlas, the compositor of TDE. Runs ~/.config/tde/autostart, or else the one TDE comes with,\n"
                "once it is up.\n\n"
                "With --greeter, runs COMMAND alone, as the login screen, and ends when it does, with its\n"
                "exit status. The displays, pointer and keyboard follow the settings copied to\n" TDE_GREETER_DIR
                "/config.\n",
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

// The greeter, which the compositor runs for as long as it runs.
struct Greeter {
    atlas::Server* server = nullptr;
    pid_t pid = -1;
    int status = 1;
};

int greeterEnded(int fd, uint32_t, void* data)
{
    auto* greeter = static_cast<Greeter*>(data);
    int status = 0;
    if (waitpid(greeter->pid, &status, WNOHANG) == greeter->pid) {
        greeter->status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        close(fd);
        greeter->server->terminate();
    }
    return 0;
}

// Starts `argv` as the compositor's child, heard of through `loop` once it ends.
bool startGreeter(Greeter& greeter, char** argv, wl_event_loop* loop)
{
    greeter.pid = fork();
    if (greeter.pid < 0)
        return false;
    if (greeter.pid == 0) {
        // The signals the compositor waits for are blocked in it, and would be in the greeter.
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        execvp(argv[0], argv);
        std::fprintf(stderr, "tde-atlas: cannot run %s\n", argv[0]);
        _exit(127);
    }
    const int fd = int(syscall(SYS_pidfd_open, greeter.pid, 0));
    if (fd < 0)
        return false;
    wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, greeterEnded, &greeter);
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    bool debug = false;
    char** greeterCommand = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) {
            debug = true;
        } else if (std::strcmp(argv[i], "--greeter") == 0 && i + 1 < argc) {
            // What follows is the greeter's, arguments and all.
            greeterCommand = argv + i + 1;
            break;
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
    if (greeterCommand) {
        // The settings, kept where the greeter's user can read them, are those an administrator
        // copied there from a session; Num Lock is kept there as it was left.
        setenv("XDG_CONFIG_HOME", TDE_GREETER_DIR "/config", true);
        setenv("XDG_STATE_HOME", TDE_GREETER_DIR "/state", true);
        setenv("XDG_SESSION_TYPE", "wayland", true);
        server.greeter = true;
        atlas::restrictSettings();
    }
    if (!server.start())
        return 1;
    wl_event_loop_add_signal(server.eventLoop, SIGTERM, terminate, &server);
    wl_event_loop_add_signal(server.eventLoop, SIGINT, terminate, &server);

    if (greeterCommand) {
        Greeter greeter {.server = &server};
        if (!startGreeter(greeter, greeterCommand, server.eventLoop)) {
            std::fprintf(stderr, "tde-atlas: cannot start the greeter\n");
            return 1;
        }
        wlr_log(WLR_INFO, "greeting on WAYLAND_DISPLAY=%s", server.socketName().c_str());
        server.run();
        // Gone with the compositor, should it have stopped first.
        if (waitpid(greeter.pid, nullptr, WNOHANG) == 0) {
            kill(greeter.pid, SIGTERM);
            waitpid(greeter.pid, nullptr, 0);
        }
        return greeter.status;
    }

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
