#pragma once

#include "Notifications.hpp"
#include "Sounds.hpp"

#include <Windows.hpp>

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
class Taskbar;
class Tray;

// The bar along the top of the screen: the applications on the left, the clock in the
// middle, with the calendar and the notifications behind it, the tray icons and the menu to
// log out or shut down on the right. Notifications also show as banners below it.
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
    void layOut();
    QMenu* createSystemMenu();
    void pickAction(uint id, const QString& action);
    void raiseWindowOf(const QString& desktopEntry, const QString& appName);

    Windows m_windows;
    NotificationServer m_notifications;
    std::unique_ptr<Banners> m_banners; // a window of its own
    Sounds m_sounds;
    Locking* m_locking = nullptr;
    Taskbar* m_taskbar = nullptr;
    QToolButton* m_clock = nullptr;
    Tray* m_tray = nullptr;
    QToolButton* m_system = nullptr;
    QTimer m_tick;
};

} // namespace hermes
