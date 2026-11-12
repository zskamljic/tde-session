#pragma once

#include "Widgets.hpp"

#include <SessionConfig.hpp>

#include <functional>

class QComboBox;

namespace daedalus {

// What the pages change: the session's settings, saved again after every change.
struct Settings {
    shell::SessionConfig config;
    std::function<void()> save;
};

// Alt+Tab, the side of the window buttons, the hot corner, the animations of the overview and
// Flip 3D, and the keys for switching windows.
class WindowsPage : public Page {
public:
    explicit WindowsPage(Settings& settings, QWidget* parent = nullptr);

    void desktopChanged(const tde::DesktopConfig& config) override;

private:
    QComboBox* m_titleBarSize = nullptr;
    QComboBox* m_buttonSide = nullptr; // of title bars, in the desktop config
};

// The clock in the bar and on the lock screen.
class DateTimePage : public Page {
public:
    explicit DateTimePage(Settings& settings, QWidget* parent = nullptr);
};

// Do not disturb, and for each application that sent notifications, whether they show and
// make a sound.
class NotificationsPage : public Page {
public:
    explicit NotificationsPage(Settings& settings, QWidget* parent = nullptr);
};

} // namespace daedalus
