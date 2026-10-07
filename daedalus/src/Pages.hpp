#pragma once

#include "Widgets.hpp"

#include <SessionConfig.hpp>

#include <functional>

namespace daedalus {

// What the pages change: the session's settings, saved again after every change.
struct Settings {
    shell::SessionConfig config;
    std::function<void()> save;
};

// Alt+Tab, the animations of the overview and Flip 3D, and the keys for switching windows.
class WindowsPage : public Page {
public:
    explicit WindowsPage(Settings& settings, QWidget* parent = nullptr);
};

// The clock in the bar and on the lock screen.
class DateTimePage : public Page {
public:
    explicit DateTimePage(Settings& settings, QWidget* parent = nullptr);
};

} // namespace daedalus
