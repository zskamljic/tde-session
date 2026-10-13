#pragma once

#include <QString>

namespace shell {

// Sets `key` of the table `table` in the desktop config at `path` to `value`, Lua as written,
// changing only that: the rest of the file, comments and all, stays as it is. What is
// missing is added. An empty `table` is the one the file returns. Returns whether it was saved.
bool setDesktopSetting(const QString& path, const QString& table, const QString& key, const QString& value);

// Puts back the commas between a table's entries that versions before 0.3.0 left out when
// adding a setting, after which the config no longer loaded, if that makes it load again.
// Returns whether it was repaired.
bool repairDesktopConfig(const QString& path);

// Why the desktop config at `path` cannot be read; empty when it can, or is not there.
QString desktopConfigError(const QString& path);

// The same for text, quoted as Lua has it.
bool setDesktopString(const QString& path, const QString& table, const QString& key, const QString& value);

} // namespace shell
