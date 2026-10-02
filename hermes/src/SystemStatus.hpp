#pragma once

#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>

namespace hermes {

// The charge of the battery, through UPower, on machines that have one.
class Battery : public QObject {
    Q_OBJECT

public:
    explicit Battery(QObject* parent = nullptr);

    bool isPresent() const { return m_present; }
    int percentage() const { return m_percentage; }
    bool isCharging() const { return m_charging; }
    // "2:15 left", "Fully charged" and the like; empty when there is nothing to say.
    QString timeText() const { return m_time; }
    QString iconName() const { return m_icon; }

signals:
    void changed();

private slots:
    void refresh();

private:
    bool m_present = false;
    int m_percentage = 0;
    bool m_charging = false;
    QString m_time;
    QString m_icon;
};

// The network connection, through NetworkManager, when it runs.
class Network : public QObject {
    Q_OBJECT

public:
    explicit Network(QObject* parent = nullptr);

    bool isAvailable() const { return m_available; }
    bool isConnected() const { return m_connected; }
    // The connection's name, or what is going on: "Connecting…", "Offline".
    QString description() const { return m_description; }
    QString iconName() const { return m_icon; }
    bool hasWifi() const { return m_wifiHardware; }
    bool isWifiEnabled() const { return m_wifi; }
    void setWifiEnabled(bool enabled);

signals:
    void changed();

private slots:
    void refresh();

private:
    QDBusServiceWatcher m_service;
    bool m_available = false;
    bool m_connected = false;
    bool m_wifi = false;
    bool m_wifiHardware = false;
    QString m_description;
    QString m_icon;
};

// The screen's backlight, on machines that have one: read from sysfs, set through logind,
// which lets the user of the active session do it.
class Brightness : public QObject {
    Q_OBJECT

public:
    explicit Brightness(QObject* parent = nullptr);

    bool isAvailable() const { return !m_device.isEmpty(); }
    // From 0 to 1.
    double value() const;
    void setValue(double value);
    // Reads it again, as other programs and the keyboard may have changed it.
    void refresh();

signals:
    void changed();

private:
    QString m_device; // its name under /sys/class/backlight
    int m_maximum = 0;
    int m_current = 0;
};

} // namespace hermes
