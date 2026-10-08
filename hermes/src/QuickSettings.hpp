#pragma once

#include <Bluetooth.hpp>
#include <Wifi.hpp>

#include <QWidget>

#include <vector>

class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace hermes {

class Audio;
class Battery;
class Brightness;
class Network;

// The panel behind the status icons of the bar: volume, brightness, network with the Wi-Fi
// networks in range, Bluetooth with the devices paired, and battery, and the buttons to lock, suspend, restart, shut
// down and log out. What the machine does not have, such as a battery, is left out.
class QuickSettings : public QWidget {
    Q_OBJECT

public:
    QuickSettings(Audio& audio, Brightness& brightness, Network& network, shell::Wifi& wifi,
        shell::Bluetooth& bluetooth, Battery& battery, QWidget* parent = nullptr);

    // Brings what may have changed unnoticed, such as the brightness, up to date.
    void refresh();

signals:
    void lockRequested();
    void suspendRequested();
    void restartRequested();
    void shutDownRequested();
    void logOutRequested();
    // The Wi-Fi network `ssid` was picked, which may want a password asked for.
    void wifiNetworkChosen(const QString& ssid);
    // The settings app is wanted, open on `page`.
    void settingsRequested(const QString& page);
    // Rows came or went; whatever holds the panel makes room for it.
    void resized();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void sync();
    QToolButton* actionButton(const QString& icon, const QString& name, void (QuickSettings::*signal)());
    QToolButton* settingsButton(const QString& page, const QString& name);
    void syncBluetooth();
    void syncWifi();
    // A button as wide as the panel: an icon, a name, and a word on the state at the right.
    QToolButton* listButton(QWidget* parent, const QString& icon, const QString& name, const QString& state);

    Audio& m_audio;
    Brightness& m_brightness;
    Network& m_network;
    shell::Wifi& m_wifi;
    shell::Bluetooth& m_bluetooth;
    Battery& m_battery;

    QWidget* m_volumeRow;
    QToolButton* m_mute;
    QSlider* m_volume;
    QWidget* m_brightnessRow;
    QSlider* m_light;
    QWidget* m_networkRow;
    QLabel* m_networkIcon;
    QLabel* m_networkText;
    QToolButton* m_wifiSwitch;
    QWidget* m_networks; // those in range, while Wi-Fi is on
    QVBoxLayout* m_networkList;
    std::vector<shell::WifiNetwork> m_shownNetworks;
    QWidget* m_bluetoothRow;
    QLabel* m_bluetoothIcon;
    QLabel* m_bluetoothText;
    QToolButton* m_bluetoothSwitch;
    QWidget* m_devices; // the paired ones, while Bluetooth is on
    QVBoxLayout* m_deviceList;
    std::vector<shell::BluetoothDevice> m_shownDevices; // as the buttons show them
    // What last went wrong, shown for a while in the row it is about.
    QString m_wifiProblem;
    QString m_bluetoothProblem;
    QWidget* m_batteryRow;
    QLabel* m_batteryIcon;
    QLabel* m_batteryText;
};

// The name of a symbolic icon for `volume` from 0 to 1, or muted.
QString volumeIconName(double volume, bool muted);

} // namespace hermes
