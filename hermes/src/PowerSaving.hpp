#pragma once

#include "Idle.hpp"

#include "wlr-output-power-management-unstable-v1-client-protocol.h"

#include <QObject>

#include <vector>

SHELL_PROXY(zwlr_output_power_manager_v1, zwlr_output_power_manager_v1_destroy);
SHELL_PROXY(zwlr_output_power_v1, zwlr_output_power_v1_destroy);

namespace hermes {

using shell::Proxy;

// Saves power when the computer is not used: the screens turn off after a while without input,
// and on again with the next, and later the computer sleeps, sooner on battery. Programs that ask
// the screen to stay on, such as video players, hold both off.
class PowerSaving : public QObject {
    Q_OBJECT

public:
    explicit PowerSaving(QObject* parent = nullptr);
    ~PowerSaving() override;

    // Minutes without input for each; 0 never does it.
    void setTimes(int blank, int suspend, int suspendOnBattery);

private slots:
    void powerSourceChanged();

private:
    void bind();
    void watch();
    void setScreensOn(bool on);
    void suspend();

    Proxy<ext_idle_notifier_v1> m_notifier;
    Proxy<zwlr_output_power_manager_v1> m_outputPower;
    Proxy<ext_idle_notification_v1> m_blankAfter;
    Proxy<ext_idle_notification_v1> m_suspendAfter;
    std::vector<Proxy<zwlr_output_power_v1>> m_screens; // while they are off
    int m_blank = 0;
    int m_suspend = 0;
    int m_suspendOnBattery = 0;
    bool m_onBattery = false;
};

} // namespace hermes
