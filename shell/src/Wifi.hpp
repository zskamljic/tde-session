#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <vector>

namespace shell {

// A Wi-Fi network in range, or one connected to before.
struct WifiNetwork {
    QString ssid;
    int strength = -1; // percent; -1 when out of range
    bool secured = false; // asks for a password
    bool active = false; // connected, or connecting
    QString accessPoint; // the strongest one in range, as NetworkManager knows it
    QString connection; // the connection kept for it; empty when it was never used
    bool passwordRejected = false; // the one kept did not work last time

    // Whether connecting needs a password typed: never used, or its password did not work.
    bool needsPassword() const { return secured && (connection.isEmpty() || passwordRejected); }

    bool operator==(const WifiNetwork&) const = default;
};

// A cable connection.
struct WiredDevice {
    QString path;
    QString name; // the interface, as "enp1s0"
    bool connected = false;
    bool pluggedIn = false;

    bool operator==(const WiredDevice&) const = default;
};

// Wi-Fi and wired networking through NetworkManager: the networks in range and those kept,
// connecting to them, and Wi-Fi on or off.
class Wifi : public QObject, protected QDBusContext {
    Q_OBJECT

public:
    explicit Wifi(QObject* parent = nullptr);

    // Whether NetworkManager runs and there is a Wi-Fi device.
    bool isAvailable() const { return !m_device.isEmpty(); }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    // In range first, the one connected to before the rest, then the strongest.
    const std::vector<WifiNetwork>& networks() const { return m_networks; }
    const std::vector<WiredDevice>& wired() const { return m_wired; }
    // The network last asked to connect to, which a failure is about.
    QString connecting() const { return m_connecting; }

    // Looks for networks again; those found come in through changed().
    void scan();
    // Connects to `ssid`: with the connection kept for it, or with `password` when it has none.
    void connectTo(const QString& ssid, const QString& password = {});
    void disconnect();
    // Forgets the connection kept for `ssid`, password and all.
    void forget(const QString& ssid);

signals:
    void changed();
    // Connecting or another request failed; `message` says why, for the user.
    void failed(const QString& message);

private slots:
    void scheduleRefresh() { m_refresh.start(); }
    void deviceStateChanged(uint state, uint previous, uint reason);

private:
    void refresh();
    void call(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments,
        const QString& failure);

    QDBusServiceWatcher m_service;
    QTimer m_refresh;
    QString m_device; // the Wi-Fi device's object path
    bool m_enabled = false;
    std::vector<WifiNetwork> m_networks;
    std::vector<WiredDevice> m_wired;
    QString m_connecting; // the network last asked to connect to
    QStringList m_rejected; // the networks whose password did not work
};

// How strong a signal of `strength` percent is, as the name of a symbolic icon.
QString wifiIconName(int strength);

} // namespace shell
