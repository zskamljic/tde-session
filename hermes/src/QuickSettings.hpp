#pragma once

#include <Bluetooth.hpp>
#include <PowerProfiles.hpp>
#include <Wifi.hpp>

#include <QWidget>

#include <vector>

class QGridLayout;
class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace hermes {

class Audio;
class Battery;
class Brightness;
class Network;
class NotificationServer;

// The panel behind the status icons of the bar: the battery and buttons to open the settings,
// lock the screen and, behind a menu, suspend, restart, shut down and log out; volume and
// brightness; and quick toggles for Wi-Fi and Bluetooth, each with a menu of the networks in
// range or the devices paired, the dark style, do not disturb, the power mode and a
// screenshot. One menu at a time opens below its button. What the machine does not have, such
// as a battery, is left out.
class QuickSettings : public QWidget {
    Q_OBJECT

public:
    QuickSettings(Audio& audio, Brightness& brightness, Network& network, shell::Wifi& wifi,
        shell::Bluetooth& bluetooth, Battery& battery, NotificationServer& notifications,
        shell::PowerProfiles& profiles, QWidget* parent = nullptr);

    // Brings what may have changed unnoticed, such as the brightness, up to date, and closes
    // the menu left open.
    void refresh();

signals:
    void lockRequested();
    void suspendRequested();
    void restartRequested();
    void shutDownRequested();
    void logOutRequested();
    // The Wi-Fi network `ssid` was picked, which may want a password asked for.
    void wifiNetworkChosen(const QString& ssid);
    // The settings app is wanted, open on `page`, or where it was for none.
    void settingsRequested(const QString& page);
    void darkStyleRequested(bool dark);
    void screenshotRequested();
    // Rows came or went; whatever holds the panel makes room for it.
    void resized();

protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // A toggle with a menu: the wide part switches it, the arrow beside it opens the menu.
    struct MenuToggle {
        QWidget* widget = nullptr;
        QToolButton* main = nullptr;
        QToolButton* arrow = nullptr;
        QLabel* icon = nullptr;
        QLabel* title = nullptr;
        QLabel* subtitle = nullptr;
        QWidget* menu = nullptr;
        QLabel* problem = nullptr; // what last went wrong, for a while
        QLabel* empty = nullptr; // said when the list has nothing
        QVBoxLayout* list = nullptr;
    };

    void sync();
    QToolButton* actionButton(
        QWidget* parent, const QString& icon, const QString& name, void (QuickSettings::*signal)());
    // A round button in the top row, for an icon.
    QToolButton* topButton(const QString& icon, const QString& name);
    // A wide button with an icon and a name, highlighted while on.
    QToolButton* toggle(const QString& name);
    void menuToggle(MenuToggle& toggle, const QString& name, const QString& settingsPage, const QString& settingsName);
    void paintMenuToggle(MenuToggle& toggle, const QString& icon, const QString& subtitle, bool on);
    // A card under the row of what opened it, with a heading and what is in it.
    QWidget* menuCard(const QString& icon, const QString& heading, QVBoxLayout*& content);
    // Opens `menu`, closing any other, or closes it when it is open already.
    void toggleMenu(QWidget* menu);
    // Puts the toggles there are two to a row, the open menu under the row of its toggle.
    void layOutToggles();
    void syncToggles();
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
    NotificationServer& m_notifications;
    shell::PowerProfiles& m_profiles;

    QLabel* m_batteryIcon;
    QLabel* m_batteryText;
    QToolButton* m_power;
    QWidget* m_powerMenu;
    QWidget* m_volumeRow;
    QToolButton* m_mute;
    QSlider* m_volume;
    QWidget* m_brightnessRow;
    QSlider* m_light;
    QGridLayout* m_grid;
    MenuToggle m_wifiToggle;
    QToolButton* m_wired; // instead, on a machine without Wi-Fi
    std::vector<shell::WifiNetwork> m_shownNetworks;
    MenuToggle m_bluetoothToggle;
    std::vector<shell::BluetoothDevice> m_shownDevices; // as the buttons show them
    QToolButton* m_darkStyle;
    QToolButton* m_quiet;
    QToolButton* m_powerMode;
    QToolButton* m_screenshot;
    QWidget* m_openMenu = nullptr;
    // What last went wrong, shown for a while in the menu it is about.
    QString m_wifiProblem;
    QString m_bluetoothProblem;
};

// A round, flat button for an icon, highlighted under the pointer and while on, as the bar's
// panels have them.
QToolButton* roundButton(QWidget* parent);

// The name of a symbolic icon for `volume` from 0 to 1, or muted.
QString volumeIconName(double volume, bool muted);

} // namespace hermes
