#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace hermes {

class Audio;
class Battery;
class Brightness;
class Network;

// The panel behind the status icons of the bar: volume, brightness, network and battery,
// and the buttons to lock, suspend, restart, shut down and log out. What the machine does
// not have, such as a battery, is left out.
class QuickSettings : public QWidget {
    Q_OBJECT

public:
    QuickSettings(Audio& audio, Brightness& brightness, Network& network, Battery& battery, QWidget* parent = nullptr);

    // Brings what may have changed unnoticed, such as the brightness, up to date.
    void refresh();

signals:
    void lockRequested();
    void suspendRequested();
    void restartRequested();
    void shutDownRequested();
    void logOutRequested();

private:
    void sync();
    QToolButton* actionButton(const QString& icon, const QString& name, void (QuickSettings::*signal)());

    Audio& m_audio;
    Brightness& m_brightness;
    Network& m_network;
    Battery& m_battery;

    QWidget* m_volumeRow;
    QToolButton* m_mute;
    QSlider* m_volume;
    QWidget* m_brightnessRow;
    QSlider* m_light;
    QWidget* m_networkRow;
    QLabel* m_networkIcon;
    QLabel* m_networkText;
    QToolButton* m_wifi;
    QWidget* m_batteryRow;
    QLabel* m_batteryIcon;
    QLabel* m_batteryText;
};

// The name of a symbolic icon for `volume` from 0 to 1, or muted.
QString volumeIconName(double volume, bool muted);

} // namespace hermes
