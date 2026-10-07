#pragma once

#include "Widgets.hpp"

#include <QString>

namespace daedalus {

// Sets how many minutes without input the screen locks after, in the desktop config at
// `path`, changing only that: the rest of the file, comments and all, stays as written.
// 0 locks only when asked. Returns whether it was saved.
bool setLockAfter(const QString& path, int minutes);

// Whether the screen locks by itself when the computer is not used, and after how long.
class LockPage : public Page {
public:
    explicit LockPage(QWidget* parent = nullptr);
};

} // namespace daedalus
