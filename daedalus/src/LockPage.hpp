#pragma once

#include "Widgets.hpp"

#include <QString>

class QComboBox;

namespace daedalus {

// Sets how many minutes without input the screen locks after, in the desktop config at
// `path`, changing only that: the rest of the file, comments and all, stays as written.
// 0 locks only when asked. Returns whether it was saved.
bool setLockAfter(const QString& path, int minutes);

// Whether the screen locks by itself when the computer is not used, and after how long, and
// whether it locks before the computer sleeps.
class LockPage : public Page {
public:
    explicit LockPage(QWidget* parent = nullptr);

    void desktopChanged(const tde::DesktopConfig& config) override;

private:
    Switch* m_enabled = nullptr;
    QComboBox* m_delay = nullptr;
    Switch* m_beforeSleep = nullptr;
};

} // namespace daedalus
