#pragma once

#include <QString>

namespace shell {

// Sets `key` of the table `table` in the desktop config at `path` to `value`, Lua as written,
// changing only that: the rest of the file, comments and all, stays as it is. What is
// missing is added. An empty `table` is the one the file returns. Returns whether it was saved.
bool setDesktopSetting(const QString& path, const QString& table, const QString& key, const QString& value);

// The same for text, quoted as Lua has it.
bool setDesktopString(const QString& path, const QString& table, const QString& key, const QString& value);

} // namespace shell
