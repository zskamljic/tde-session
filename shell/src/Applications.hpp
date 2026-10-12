#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace shell {

// An installed application, from its .desktop file.
struct Application {
    QString id; // e.g. "org.gnome.TextEditor.desktop"
    QString name;
    QString genericName; // what it is, like "Text Editor"
    QString comment;
    QStringList keywords;
    QString exec;
    QString icon;
    QString workingDirectory;
    QString startupWmClass; // the app id its windows have, when not the file's name
    QStringList mimeTypes; // what it opens
    QStringList categories; // as the menus sort it, "TerminalEmulator" among them
    bool terminal = false;
    bool inMenus = true; // false for helpers and apps of other desktops, which only lend their icons
};

// Splits a desktop entry's Exec value into arguments: spaces separate, double quotes group.
QStringList splitExec(const QString& exec);

// The command line that starts `app` without files: field codes expanded or dropped.
QStringList commandLine(const Application& app);

// The applications of the XDG data directories, sorted by name; those that belong in menus
// of `desktops` (the names in XDG_CURRENT_DESKTOP) are marked so. Entries in earlier folders,
// the user's own first, hide those with the same id further on.
std::vector<Application> loadApplications(const QStringList& desktops);

// The application whose windows have `appId`: by file name, by StartupWMClass, then by the
// program it runs. Null when none matches.
const Application* findApplication(const std::vector<Application>& apps, const QString& appId);

// The applications for menus matching every word of `query`, best match first: names starting with
// it, then names with a word starting with it, then what the apps are and their keywords.
std::vector<const Application*> search(const std::vector<Application>& apps, const QString& query);

// The command line that starts `app` in its own scope of the user's systemd, as desktops do,
// so it is not counted with the program starting it, and in a terminal when it asks for one.
QStringList launchCommand(const Application& app, bool systemd);

// Starts `app` detached from the program asking; false when it could not be started.
bool launch(const Application& app);

// The folders applications are read from, to notice when some are installed or removed.
QStringList applicationFolders();

// The .desktop file of the application with the id `id`, as the XDG data folders have it;
// empty when there is none.
QString applicationFile(const QString& id);

// A program started at login, from the XDG autostart folders.
struct AutostartEntry {
    QString id; // the file's name, e.g. "nm-applet.desktop"
    QString path; // the file that counts: the user's own, else the system's
    QString name;
    QString comment;
    bool enabled = true; // neither Hidden nor X-GNOME-Autostart-enabled=false
    bool system = false; // one of that name is in a folder of the system
    bool user = false; // the user has one of their own, which counts instead
};

// The autostart folders: the user's first, then those of the system.
QStringList autostartFolders();

// The entries of `folders`, the user's first, which start in `desktops` and are meant to be
// shown, sorted by name; one in an earlier folder hides those of its name further on.
std::vector<AutostartEntry> autostartEntries(const QStringList& folders, const QStringList& desktops);

} // namespace shell
