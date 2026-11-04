#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace charon {

// A kind of session there is to log into, as a display manager lists them: from the desktop
// files of wayland-sessions, and of xsessions when X can be started.
struct Session {
    std::string id; // the file's name without .desktop, as XDG_SESSION_DESKTOP has it
    std::string name; // in the language of the login screen
    std::string exec;
    std::vector<std::string> desktopNames; // for XDG_CURRENT_DESKTOP
    bool x11 = false;
};

// The session the desktop file `text` describes, named in `language` (as LANG has it) when it
// has a name in it; false for one that is hidden or cannot run.
bool parseSession(std::string_view text, std::string_view language, Session& session);

// Those of XDG_DATA_DIRS, the first of each id, sorted by name; X11 sessions only when `x11`.
std::vector<Session> findSessions(bool x11);

// What greetd runs for `session`: through `shell` as a login shell, as GDM does, so that the
// profiles it reads set up the environment. An X session is started by startx, which splits
// whatever it is given at its spaces; so the session's command does not go through it but
// waits in the environment, as x11SessionVariable, for `tde-charon --x11-session` to run.
std::vector<std::string> sessionCommand(const Session& session, const std::string& shell);
inline constexpr const char* x11SessionVariable = "TDE_X11_SESSION";
// `text` in single quotes, as the shell takes it whatever it holds.
std::string shellQuoted(const std::string& text);
// The words of `command` as one line that /bin/sh takes apart into them again. greetd does not
// run the words it is given as they are: it joins them with spaces for /bin/sh, so a word
// with a space in it, as the command a login shell is to run, would fall apart into several.
std::string commandLine(const std::vector<std::string>& command);
// What is added to the environment PAM gives the session.
std::vector<std::string> sessionEnvironment(const Session& session);

} // namespace charon
