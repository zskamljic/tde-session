#pragma once

#include "Widgets.hpp"

#include <Applications.hpp>

#include <QString>

namespace daedalus {

// The text of a desktop entry with it set to start at login or not: Hidden, which the
// specification and systemd go by, and X-GNOME-Autostart-enabled, which GNOME's tools write,
// in its [Desktop Entry] group; the rest stays as it is.
QString withAutostartEnabled(const QString& text, bool enabled);

// Starts `entry` at login or not, by the user's own copy of it in `userFolder`.
bool setAutostartEnabled(const shell::AutostartEntry& entry, bool enabled, const QString& userFolder);
// Starts the installed application with the id `id` at login.
bool addAutostart(const QString& id, const QString& userFolder);

// The programs that start at login, from the XDG autostart folders, to turn on and off; and
// applications to add to them, or remove again.
class StartupPage : public Page {
public:
    explicit StartupPage(QWidget* parent = nullptr);

private:
    void fill();
    void add();

    Group* m_group = nullptr;
};

} // namespace daedalus
