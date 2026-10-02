#pragma once

#include <Applications.hpp>

#include <QString>
#include <QStringList>

#include <vector>

namespace hermes {

// What the taskbar needs to know of a window.
struct OpenWindow {
    quint64 id = 0;
    QString appId;
    bool activated = false;
    bool minimized = false;
};

// One button on the taskbar: the windows of an application, or a window no installed
// application claims.
struct Group {
    QString key; // the desktop file's id, or the windows' app id when no application has them
    const shell::Application* app = nullptr;
    std::vector<quint64> windows; // in the order they opened
};

// The buttons for `windows`, in the order the first window of each opened.
std::vector<Group> groupWindows(const std::vector<OpenWindow>& windows, const std::vector<shell::Application>& apps);

// What a click on a group's button does, Windows 7 style.
struct Click {
    enum Action {
        Activate, // switch to `window`
        Minimize, // the only window, already active: out of the way
    } action;
    quint64 window = 0;
};

// With one window: switch to it, or minimize it when it is active. With several: the one used
// most recently (first in `recent`), and the next of them on every further click.
Click click(const Group& group, const std::vector<OpenWindow>& windows, const std::vector<quint64>& recent);

// Whether a window with `appId` belongs to the program that sent a notification with
// `desktopEntry` (the hint, often empty) and `appName`: "org.gnome.TextEditor" for
// "org.gnome.TextEditor" or "Text Editor", "firefox" for "Firefox".
bool isWindowOf(const QString& appId, const QString& desktopEntry, const QString& appName);

} // namespace hermes
