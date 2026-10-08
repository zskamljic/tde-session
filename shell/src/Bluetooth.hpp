#pragma once

#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <vector>

namespace shell {

// A device BlueZ knows of: paired before, or seen nearby while looking for devices.
struct BluetoothDevice {
    QString path; // its object on the system bus
    QString name;
    QString address;
    QString icon; // as BlueZ names its kind: audio-headphones, input-mouse and the like
    bool paired = false;
    bool connected = false;
    int battery = -1; // percent, when the device tells

    bool operator==(const BluetoothDevice&) const = default;
};

// Bluetooth, through BlueZ: the first adapter, whether it is on, and its devices. Changes
// made elsewhere, as with bluetoothctl, show here too.
class Bluetooth : public QObject {
    Q_OBJECT

public:
    explicit Bluetooth(QObject* parent = nullptr);

    // Whether BlueZ runs and there is an adapter.
    bool isAvailable() const { return !m_adapter.isEmpty(); }
    bool isPowered() const { return m_powered; }
    void setPowered(bool powered);
    // Looking for devices nearby, which then show among the devices.
    bool isDiscovering() const { return m_discovering; }
    void setDiscovering(bool discovering);
    QString name() const { return m_name; } // how other devices see this computer
    // Whether other devices looking for some find this computer.
    void setDiscoverable(bool discoverable);

    // Paired ones first, connected ones before the rest, then by name.
    const std::vector<BluetoothDevice>& devices() const { return m_devices; }
    std::vector<BluetoothDevice> paired() const;
    // The connected devices' names, for a line of text; empty when none is.
    QString connectedNames() const;

    void connectDevice(const QString& path);
    void disconnectDevice(const QString& path);
    // Pairs, then trusts and connects it.
    void pair(const QString& path);
    // Forgets a paired device.
    void remove(const QString& path);

signals:
    void changed();
    // A request failed; `message` says why, for the user.
    void failed(const QString& message);

private slots:
    void scheduleRefresh() { m_refresh.start(); }

private:
    void refresh();
    void call(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments,
        const QString& failure, std::function<void()> then = {});

    QDBusServiceWatcher m_service;
    QTimer m_refresh; // changes come in bursts; they are read once
    QString m_adapter; // its object path; empty without one
    bool m_powered = false;
    bool m_discovering = false;
    QString m_name;
    std::vector<BluetoothDevice> m_devices;
};

} // namespace shell
