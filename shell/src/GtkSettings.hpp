#pragma once

#include <QString>

namespace shell {

// Sets `key` in the [Settings] group of GTK's settings.ini at `path` to `value`, changing only
// that line: the rest of the file stays as it is, and what is missing is added. GTK programs
// running under X11, with no XSETTINGS manager to ask, take their settings from there.
// Returns whether it was saved.
bool setGtkSetting(const QString& path, const QString& key, const QString& value);

// The same in GTK 3's and GTK 4's settings.ini, under `configDir` (~/.config).
bool setGtkSettings(const QString& configDir, const QString& key, const QString& value);

} // namespace shell
