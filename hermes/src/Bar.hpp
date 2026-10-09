#pragma once

#include "Audio.hpp"
#include "DisplayLayouts.hpp"
#include "Media.hpp"
#include "Notifications.hpp"
#include "Osd.hpp"
#include "Sounds.hpp"
#include "SystemStatus.hpp"

#include <Bluetooth.hpp>
#include <PowerProfiles.hpp>
#include <Wifi.hpp>
#include <Windows.hpp>

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <memory>

class QMenu;
class QToolButton;

namespace hermes {

using shell::Window;
using shell::Windows;

class Banners;
class Locking;
class PowerSaving;
class PolkitAgent;
class Taskbar;
class Tray;

// The bar along the top of the screen: the applications on the left, the clock in the
// middle, with the calendar, the media player and the notifications behind it, and on the
// right the tray icons and the status of sound, network and battery, behind which are the
// quick settings. Notifications also show as banners below it.
class Bar : public QWidget {
    Q_OBJECT

public:
    explicit Bar(QWidget* parent = nullptr);
    ~Bar() override;

    bool isSupported() const { return m_windows.isSupported(); }

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateClock();
    // Puts the bar, the notifications and the level indicator on `screen`.
    void placeOn(QScreen* screen);
    void layOut();
    QMenu* createQuickSettings();
    QMenu* createCalendarMenu();
    // Opens the calendar's panel, centred below the clock.
    void showCalendar();
    void updateStatusIcon();
    void pickAction(uint id, const QString& action);
    void raiseWindowOf(const QString& desktopEntry, const QString& appName);

    Windows m_windows;
    NotificationServer m_notifications;
    std::unique_ptr<Banners> m_banners; // a window of its own
    Sounds m_sounds;
    Locking* m_locking = nullptr;
    PowerSaving* m_powerSaving = nullptr;
    PolkitAgent* m_polkit = nullptr;
    Taskbar* m_taskbar = nullptr;
    QToolButton* m_clock = nullptr;
    QMenu* m_calendarMenu = nullptr; // behind the clock
    QElapsedTimer m_calendarClosed; // to tell a click that closed it from one that opens it
    Tray* m_tray = nullptr;
    QToolButton* m_system = nullptr; // the status icons, which open the quick settings
    Audio m_audio;
    Brightness m_brightness;
    Network m_network;
    shell::Wifi m_wifi;
    shell::Bluetooth m_bluetooth;
    Battery m_battery;
    Media m_media;
    shell::PowerProfiles m_profiles;
    MediaKeys m_keys {m_audio, m_brightness, m_media}; // with the level indicator, a window of its own
    DisplayLayouts m_displays;
    QTimer m_tick;
    bool m_seconds = false; // shown in the clock
};

} // namespace hermes
