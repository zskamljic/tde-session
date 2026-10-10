#pragma once

#include "Pages.hpp"

#include <PowerProfiles.hpp>

#include <QTimer>

#include <functional>

namespace daedalus {

// How the computer saves power: the batteries, its power mode, when the screens turn off, and
// when it sleeps by itself, plugged in and on battery.
class PowerPage : public Page {
public:
    explicit PowerPage(Settings& settings, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    shell::PowerProfiles m_profiles;
    QTimer m_batteryTimer;
    std::function<void()> m_batteryUpdate; // shows the batteries as they are now
};

} // namespace daedalus
