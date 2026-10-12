#pragma once

#include <QString>
#include <QStringList>

#include <vector>

// The themes installed for icons, pointers and GTK programs, to pick from, and the GNOME
// settings GTK programs, Xwayland and the portals take them from.
namespace daedalus {

struct Theme {
    QString id; // the folder's name, which settings name it by
    QString name; // as it calls itself

    bool operator==(const Theme&) const = default;
};

// The folders icon and pointer themes are in, the user's first: ~/.icons and the "icons" of
// the XDG data folders.
QStringList iconFolders();
// The folders GTK themes are in: ~/.themes and the "themes" of the XDG data folders.
QStringList themeFolders();

// Icon themes in `folders`, by name: those with icons in them, and not hidden.
std::vector<Theme> iconThemes(const QStringList& folders = iconFolders());
// Pointer themes in `folders`, by name: those with a "cursors" folder.
std::vector<Theme> cursorThemes(const QStringList& folders = iconFolders());
// GTK 3 themes in `folders`, by name, with Adwaita and its dark style, which GTK has built in.
std::vector<Theme> gtkThemes(const QStringList& folders = themeFolders());

// A key of org.gnome.desktop.interface, as text; empty when gsettings cannot tell.
QString gnomeSetting(const QString& key);
// Sets one, in the background: `value` as GVariant text, quoted for strings.
void setGnomeSetting(const QString& key, const QString& value);
void setGnomeString(const QString& key, const QString& value);

} // namespace daedalus
