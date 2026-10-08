#pragma once

#include "Widgets.hpp"

#include <Bluetooth.hpp>

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QSet>

#include <memory>
#include <vector>

namespace tde {
class Toast;
}

namespace daedalus {

// Answers BlueZ while devices pair: shows the code a keyboard is to type, asks whether the
// code a phone shows matches, and asks for the PIN of devices that want one. Registered while
// the settings are open.
class PairingAgent : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    PairingAgent(const shell::Bluetooth& bluetooth, QWidget* window);
    ~PairingAgent() override;

public slots:
    void Release() { }
    QString RequestPinCode(const QDBusObjectPath& device);
    void DisplayPinCode(const QDBusObjectPath& device, const QString& pinCode);
    uint RequestPasskey(const QDBusObjectPath& device);
    void DisplayPasskey(const QDBusObjectPath& device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath& device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath& device);
    void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
    void Cancel() { }

private:
    QString nameOf(const QDBusObjectPath& device) const;
    void reject();

    const shell::Bluetooth& m_bluetooth;
    QWidget* m_window;
    tde::Toast* m_toast;
    bool m_registered = false;
};

// Bluetooth on or off, the devices paired, connecting and forgetting them, and pairing those
// nearby, which are looked for while the page shows.
class BluetoothPage : public Page {
public:
    explicit BluetoothPage(QWidget* parent = nullptr);
    ~BluetoothPage() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void sync();
    QWidget* deviceControls(const shell::BluetoothDevice& device);

    shell::Bluetooth m_bluetooth;
    std::unique_ptr<PairingAgent> m_agent;
    QWidget* m_content = nullptr; // the groups, made again as Bluetooth changes
    tde::Toast* m_toast = nullptr;
    std::vector<shell::BluetoothDevice> m_shown;
    bool m_shownPowered = false;
    QSet<QString> m_pairing; // the devices being paired, whose buttons say so
};

} // namespace daedalus
