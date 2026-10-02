#pragma once

#include "ext-idle-notify-v1-client-protocol.h"

#include <Proxy.hpp>

#include <QDBusUnixFileDescriptor>
#include <QObject>

#include <functional>
#include <vector>

SHELL_PROXY(ext_idle_notifier_v1, ext_idle_notifier_v1_destroy);
SHELL_PROXY(ext_idle_notification_v1, ext_idle_notification_v1_destroy);

class QProcess;

namespace hermes {

using shell::Proxy;

// Locks the screen with Cerberus when it should be: after a while of no input (unless a
// program, such as a video player, asks otherwise), before the computer sleeps, and when
// logind is asked to, as by `loginctl lock-session`.
class Locking : public QObject {
    Q_OBJECT

public:
    explicit Locking(QObject* parent = nullptr);
    ~Locking() override;

    // Starts the lock screen, unless it runs already; `whenLocked` runs once the session is
    // locked, or when it could not be.
    void lock(std::function<void()> whenLocked = {});
    // How long without input until the screen locks; 0 for never.
    void setIdleMinutes(int minutes);

private slots:
    void prepareForSleep(bool sleeping);
    void lockRequested();

private:
    void bindIdle();
    void takeSleepInhibitor();
    void settled();

    QProcess* m_locker = nullptr;
    int m_idleMinutes = 0;
    bool m_locked = false;
    std::vector<std::function<void()>> m_waiting;
    QDBusUnixFileDescriptor m_sleepInhibitor;
    Proxy<ext_idle_notifier_v1> m_notifier;
    Proxy<ext_idle_notification_v1> m_idle;
};

} // namespace hermes
