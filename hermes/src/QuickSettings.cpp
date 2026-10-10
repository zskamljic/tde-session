#include "QuickSettings.hpp"

#include "Audio.hpp"
#include "Notifications.hpp"
#include "SystemStatus.hpp"

#include <Icons.hpp>
#include <Layouts.hpp>
#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int IconSize = 16;
constexpr int Steps = 100;

QIcon symbolic(const QString& name, QWidget* widget)
{
    return shell::tintedIcon(name, QSize(IconSize, IconSize), widget->devicePixelRatioF(), tde::theme::colors().text);
}

QWidget* row(QWidget* parent, QHBoxLayout*& layout)
{
    auto* widget = new QWidget(parent);
    layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    return widget;
}

// A level from 0 to Steps, with a round knob on a thin track, filled in up to it.
QSlider* slider(QWidget* parent)
{
    const auto& colors = tde::theme::colors();
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(0, Steps);
    slider->setMinimumHeight(24);
    slider->setStyleSheet(
        u"QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %1; }"
        " QSlider::sub-page:horizontal { height: 4px; border-radius: 2px; background: %2; }"
        " QSlider::handle:horizontal { width: 18px; height: 18px; margin: -7px 0;"
        " border-radius: 9px; background: %3; }"
        " QSlider::handle:horizontal:hover { background: %4; }"_s.arg(colors.pressed.name(QColor::HexArgb),
            colors.accent.name(), QColor(0xf0f0f0).name(), QColor(Qt::white).name()));
    return slider;
}

} // namespace

QToolButton* roundButton(QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setIconSize(QSize(IconSize, IconSize));
    button->setFixedSize(32, 32);
    const auto& colors = tde::theme::colors();
    button->setStyleSheet(u"QToolButton { background: transparent; border: none; border-radius: 16px; }"
                          " QToolButton:hover { background: %1; }"
                          " QToolButton:pressed, QToolButton:checked { background: %2; }"_s.arg(
                              colors.hover.name(QColor::HexArgb), colors.pressed.name(QColor::HexArgb)));
    return button;
}

QString volumeIconName(double volume, bool muted)
{
    if (muted || volume <= 0)
        return u"audio-volume-muted-symbolic"_s;
    if (volume < 0.34)
        return u"audio-volume-low-symbolic"_s;
    if (volume < 0.67)
        return u"audio-volume-medium-symbolic"_s;
    return u"audio-volume-high-symbolic"_s;
}

QuickSettings::QuickSettings(Audio& audio, Brightness& brightness, Network& network, shell::Wifi& wifi,
    shell::Bluetooth& bluetooth, Battery& battery, NotificationServer& notifications, shell::PowerProfiles& profiles,
    QWidget* parent)
    : QWidget(parent)
    , m_audio(audio)
    , m_brightness(brightness)
    , m_network(network)
    , m_wifi(wifi)
    , m_bluetooth(bluetooth)
    , m_battery(battery)
    , m_notifications(notifications)
    , m_profiles(profiles)
{
    setFixedWidth(340);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    QHBoxLayout* line = nullptr;
    m_volumeRow = row(this, line);
    m_mute = roundButton(m_volumeRow);
    m_mute->setToolTip(u"Mute"_s);
    connect(m_mute, &QToolButton::clicked, this, [this] { m_audio.setMuted(!m_audio.isMuted()); });
    m_volume = slider(m_volumeRow);
    connect(m_volume, &QSlider::valueChanged, this, [this](int value) { m_audio.setVolume(double(value) / Steps); });
    line->addWidget(m_mute);
    line->addWidget(m_volume, 1);
    layout->addWidget(m_volumeRow);

    m_brightnessRow = row(this, line);
    auto* sun = new QLabel(m_brightnessRow);
    sun->setFixedSize(32, 32);
    sun->setAlignment(Qt::AlignCenter);
    sun->setPixmap(symbolic(u"display-brightness-symbolic"_s, this).pixmap(IconSize, IconSize));
    m_light = slider(m_brightnessRow);
    connect(m_light, &QSlider::valueChanged, this, [this](int value) { m_brightness.setValue(double(value) / Steps); });
    line->addWidget(sun);
    line->addWidget(m_light, 1);
    layout->addWidget(m_brightnessRow);

    // Two by two, those the machine has.
    auto* toggles = new QWidget(this);
    auto* grid = new QGridLayout(toggles);
    grid->setContentsMargins(0, 2, 0, 2);
    grid->setSpacing(8);
    m_darkStyle = toggle(u"Dark Style"_s);
    connect(m_darkStyle, &QToolButton::clicked, this, [this](bool on) { emit darkStyleRequested(on); });
    m_quiet = toggle(u"Do Not Disturb"_s);
    m_quiet->setToolTip(u"Notifications go to the list behind the clock without a banner or a sound"_s);
    connect(m_quiet, &QToolButton::clicked, this, [this](bool on) { m_notifications.setQuiet(on); });
    m_powerMode = toggle(QString());
    m_powerMode->setCheckable(false);
    m_powerMode->setToolTip(u"Power mode: a click switches to the next"_s);
    connect(m_powerMode, &QToolButton::clicked, this, [this] {
        const QStringList offered = m_profiles.offered();
        if (offered.isEmpty())
            return;
        m_profiles.setActive(offered[(offered.indexOf(m_profiles.active()) + 1) % offered.size()]);
    });
    m_screenshot = toggle(u"Screenshot"_s);
    m_screenshot->setCheckable(false);
    connect(m_screenshot, &QToolButton::clicked, this, &QuickSettings::screenshotRequested);
    grid->addWidget(m_darkStyle, 0, 0);
    grid->addWidget(m_quiet, 0, 1);
    grid->addWidget(m_powerMode, 1, 0);
    grid->addWidget(m_screenshot, 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    layout->addWidget(toggles);

    m_networkRow = row(this, line);
    m_networkIcon = new QLabel(m_networkRow);
    m_networkIcon->setFixedSize(32, 32);
    m_networkIcon->setAlignment(Qt::AlignCenter);
    m_networkText = new QLabel(m_networkRow);
    m_networkText->setTextFormat(Qt::PlainText);
    m_wifiSwitch = roundButton(m_networkRow);
    m_wifiSwitch->setCheckable(true);
    m_wifiSwitch->setToolTip(u"Wi-Fi"_s);
    m_wifiSwitch->setIcon(symbolic(u"network-wireless-symbolic"_s, this));
    connect(m_wifiSwitch, &QToolButton::toggled, this, [this](bool on) { m_network.setWifiEnabled(on); });
    line->addWidget(m_networkIcon);
    line->addWidget(m_networkText, 1);
    line->addWidget(m_wifiSwitch);
    line->addWidget(settingsButton(u"network"_s, u"Network Settings"_s));
    layout->addWidget(m_networkRow);
    m_networks = new QWidget(this);
    m_networkList = new QVBoxLayout(m_networks);
    m_networkList->setContentsMargins(42, 0, 0, 0);
    m_networkList->setSpacing(2);
    layout->addWidget(m_networks);

    m_bluetoothRow = row(this, line);
    m_bluetoothIcon = new QLabel(m_bluetoothRow);
    m_bluetoothIcon->setFixedSize(32, 32);
    m_bluetoothIcon->setAlignment(Qt::AlignCenter);
    m_bluetoothText = new QLabel(m_bluetoothRow);
    m_bluetoothText->setTextFormat(Qt::PlainText);
    m_bluetoothSwitch = roundButton(m_bluetoothRow);
    m_bluetoothSwitch->setCheckable(true);
    m_bluetoothSwitch->setToolTip(u"Bluetooth"_s);
    m_bluetoothSwitch->setIcon(symbolic(u"bluetooth-active-symbolic"_s, this));
    connect(m_bluetoothSwitch, &QToolButton::toggled, this, [this](bool on) { m_bluetooth.setPowered(on); });
    line->addWidget(m_bluetoothIcon);
    line->addWidget(m_bluetoothText, 1);
    line->addWidget(m_bluetoothSwitch);
    line->addWidget(settingsButton(u"bluetooth"_s, u"Bluetooth Settings"_s));
    layout->addWidget(m_bluetoothRow);
    // Under it, indented past its icon.
    m_devices = new QWidget(this);
    m_deviceList = new QVBoxLayout(m_devices);
    m_deviceList->setContentsMargins(42, 0, 0, 0);
    m_deviceList->setSpacing(2);
    layout->addWidget(m_devices);

    m_batteryRow = row(this, line);
    m_batteryIcon = new QLabel(m_batteryRow);
    m_batteryIcon->setFixedSize(32, 32);
    m_batteryIcon->setAlignment(Qt::AlignCenter);
    m_batteryText = new QLabel(m_batteryRow);
    m_batteryText->setTextFormat(Qt::PlainText);
    line->addWidget(m_batteryIcon);
    line->addWidget(m_batteryText, 1);
    layout->addWidget(m_batteryRow);

    auto* separator = new QWidget(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet(u"background: %1;"_s.arg(tde::theme::colors().border.name()));
    layout->addWidget(separator);

    // One below the other, each saying what it does; those that ask first end in an ellipsis.
    auto* actions = new QWidget(this);
    auto* column = new QVBoxLayout(actions);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    QToolButton* settings = actionButton(u"preferences-system-symbolic"_s, u"Settings"_s, nullptr);
    connect(settings, &QToolButton::clicked, this, [this] { emit settingsRequested({}); });
    column->addWidget(settings);
    column->addWidget(
        actionButton(u"system-lock-screen-symbolic"_s, u"Lock the screen"_s, &QuickSettings::lockRequested));
    column->addWidget(actionButton(
        u"media-playback-pause-symbolic"_s, u"Suspend, keeping everything open"_s, &QuickSettings::suspendRequested));
    column->addWidget(
        actionButton(u"system-reboot-symbolic"_s, u"Restart the computer…"_s, &QuickSettings::restartRequested));
    column->addWidget(
        actionButton(u"system-shutdown-symbolic"_s, u"Shut down the computer…"_s, &QuickSettings::shutDownRequested));
    column->addWidget(
        actionButton(u"system-log-out-symbolic"_s, u"Log out of this session…"_s, &QuickSettings::logOutRequested));
    layout->addWidget(actions);

    connect(&m_audio, &Audio::changed, this, &QuickSettings::sync);
    connect(&m_brightness, &Brightness::changed, this, &QuickSettings::sync);
    connect(&m_network, &Network::changed, this, &QuickSettings::sync);
    connect(&m_bluetooth, &shell::Bluetooth::changed, this, &QuickSettings::sync);
    connect(&m_wifi, &shell::Wifi::changed, this, &QuickSettings::sync);
    // What went wrong shows in place of what its row says, for a while.
    const auto showProblem = [this](QString QuickSettings::* problem) {
        return [this, problem](const QString& message) {
            this->*problem = message;
            sync();
            QTimer::singleShot(6000, this, [this, problem, message] {
                if (this->*problem == message) {
                    (this->*problem).clear();
                    sync();
                }
            });
        };
    };
    connect(&m_bluetooth, &shell::Bluetooth::failed, this, showProblem(&QuickSettings::m_bluetoothProblem));
    connect(&m_wifi, &shell::Wifi::failed, this, showProblem(&QuickSettings::m_wifiProblem));
    connect(&m_battery, &Battery::changed, this, &QuickSettings::sync);
    connect(&m_notifications, &NotificationServer::changed, this, &QuickSettings::syncToggles);
    connect(&m_profiles, &shell::PowerProfiles::changed, this, &QuickSettings::syncToggles);
    sync();
}

QToolButton* QuickSettings::toggle(const QString& name)
{
    const auto& colors = tde::theme::colors();
    auto* button = new QToolButton(this);
    button->setCheckable(true);
    button->setText(name);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(QSize(IconSize, IconSize));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFixedHeight(40);
    button->setStyleSheet(u"QToolButton { background: %1; border: none; border-radius: 20px; padding: 0 14px;"
                          " text-align: left; }"
                          " QToolButton:hover { background: %2; }"
                          " QToolButton:checked { background: %3; color: %4; }"
                          " QToolButton:checked:hover { background: %5; }"_s.arg(colors.pressed.name(QColor::HexArgb),
                              colors.hover.name(QColor::HexArgb), colors.accent.name(), colors.accentText.name(),
                              colors.accent.lighter(115).name()));
    return button;
}

void QuickSettings::syncToggles()
{
    const auto& colors = tde::theme::colors();
    const auto setIcon = [this, &colors](QToolButton* button, const QString& name) {
        button->setIcon(shell::tintedIcon(name, QSize(IconSize, IconSize), devicePixelRatioF(),
            button->isChecked() ? colors.accentText : colors.text));
    };
    // Dark when the theme is, or when it follows the system and that is dark.
    const QString theme = tde::desktop().appearance.theme;
    const bool dark = theme == u"arc-dark"
        || (theme == u"system" && QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light);
    m_darkStyle->setChecked(dark);
    setIcon(m_darkStyle, u"weather-clear-night-symbolic"_s);
    m_quiet->setChecked(m_notifications.isQuiet());
    setIcon(m_quiet,
        m_notifications.isQuiet() ? u"notifications-disabled-symbolic"_s
                                  : u"preferences-system-notifications-symbolic"_s);
    m_powerMode->setVisible(m_profiles.isAvailable() && !m_profiles.offered().isEmpty());
    m_powerMode->setText(shell::powerProfileName(m_profiles.active()));
    setIcon(m_powerMode, shell::powerProfileIcon(m_profiles.active()));
    setIcon(m_screenshot, u"camera-photo-symbolic"_s);
}

QToolButton* QuickSettings::actionButton(const QString& icon, const QString& name, void (QuickSettings::*signal)())
{
    QToolButton* button = roundButton(this);
    // As wide as the panel, the icon before the words with room between them.
    button->setMinimumSize(0, 36);
    button->setMaximumSize(QWIDGETSIZE_MAX, 36);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(button->styleSheet() + u" QToolButton { border-radius: 8px; }"_s);
    button->setAccessibleName(name);
    auto* content = new QHBoxLayout(button);
    content->setContentsMargins(12, 0, 12, 0);
    content->setSpacing(12);
    auto* iconLabel = new QLabel(button);
    iconLabel->setPixmap(symbolic(icon, this).pixmap(IconSize, IconSize));
    auto* text = new QLabel(name, button);
    for (QLabel* label : {iconLabel, text})
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    content->addWidget(iconLabel);
    content->addWidget(text, 1);
    if (signal)
        connect(button, &QToolButton::clicked, this, signal);
    return button;
}

QToolButton* QuickSettings::settingsButton(const QString& page, const QString& name)
{
    QToolButton* button = roundButton(this);
    button->setToolTip(name);
    button->setIcon(symbolic(u"go-next-symbolic"_s, this));
    connect(button, &QToolButton::clicked, this, [this, page] { emit settingsRequested(page); });
    return button;
}

void QuickSettings::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit resized();
}

void QuickSettings::refresh()
{
    m_brightness.refresh();
    m_wifi.scan();
    sync();
}

void QuickSettings::sync()
{
    // Values set here are what the system says; they are not sent back to it.
    m_volumeRow->setVisible(m_audio.isAvailable());
    m_mute->setIcon(symbolic(volumeIconName(m_audio.volume(), m_audio.isMuted()), this));
    m_volume->setToolTip(m_audio.output());
    {
        const QSignalBlocker blocker(m_volume);
        m_volume->setValue(int(std::lround(m_audio.volume() * Steps)));
    }

    m_brightnessRow->setVisible(m_brightness.isAvailable());
    {
        const QSignalBlocker blocker(m_light);
        m_light->setValue(int(std::lround(m_brightness.value() * Steps)));
    }

    m_networkRow->setVisible(m_network.isAvailable());
    m_networkIcon->setPixmap(symbolic(m_network.iconName(), this).pixmap(IconSize, IconSize));
    m_networkText->setText(m_wifiProblem.isEmpty() ? m_network.description() : m_wifiProblem);
    m_networkText->setWordWrap(!m_wifiProblem.isEmpty());
    m_wifiSwitch->setVisible(m_network.hasWifi());
    {
        const QSignalBlocker blocker(m_wifiSwitch);
        m_wifiSwitch->setChecked(m_network.isWifiEnabled());
    }

    syncWifi();
    syncBluetooth();
    syncToggles();

    m_batteryRow->setVisible(m_battery.isPresent());
    m_batteryIcon->setPixmap(symbolic(m_battery.iconName(), this).pixmap(IconSize, IconSize));
    const QString time = m_battery.timeText();
    m_batteryText->setText(
        time.isEmpty() ? u"%1%"_s.arg(m_battery.percentage()) : u"%1%  ·  %2"_s.arg(m_battery.percentage()).arg(time));
    adjustSize();
}

void QuickSettings::syncBluetooth()
{
    const bool on = m_bluetooth.isPowered();
    m_bluetoothRow->setVisible(m_bluetooth.isAvailable());
    m_bluetoothIcon->setPixmap(symbolic(on ? u"bluetooth-active-symbolic"_s : u"bluetooth-disabled-symbolic"_s, this)
            .pixmap(IconSize, IconSize));
    const QString connected = m_bluetooth.connectedNames();
    m_bluetoothText->setText(!m_bluetoothProblem.isEmpty() ? m_bluetoothProblem
            : !on                                          ? u"Bluetooth is off"_s
            : connected.isEmpty()                          ? u"Bluetooth is on"_s
                                                           : connected);
    m_bluetoothText->setWordWrap(!m_bluetoothProblem.isEmpty());
    {
        const QSignalBlocker blocker(m_bluetoothSwitch);
        m_bluetoothSwitch->setChecked(on);
    }

    // A button for each device paired, connecting it or letting it go.
    auto devices = on ? m_bluetooth.paired() : std::vector<shell::BluetoothDevice> {};
    m_devices->setVisible(!devices.empty());
    if (devices == m_shownDevices)
        return;
    m_shownDevices = std::move(devices);
    shell::clearLayout(*m_deviceList);
    for (const shell::BluetoothDevice& device : m_shownDevices) {
        QToolButton* button = listButton(m_devices,
            device.icon.isEmpty() ? u"bluetooth-active-symbolic"_s : device.icon + u"-symbolic"_s, device.name,
            device.connected ? u"Connected"_s : QString());
        button->setToolTip(device.connected ? u"Disconnect"_s : u"Connect"_s);
        connect(button, &QToolButton::clicked, this, [this, path = device.path, connected = device.connected] {
            if (connected)
                m_bluetooth.disconnectDevice(path);
            else
                m_bluetooth.connectDevice(path);
        });
        m_deviceList->addWidget(button);
    }
}

QToolButton* QuickSettings::listButton(QWidget* parent, const QString& icon, const QString& name, const QString& state)
{
    QToolButton* button = roundButton(parent);
    button->setMinimumSize(0, 30);
    button->setMaximumSize(QWIDGETSIZE_MAX, 30);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(button->styleSheet() + u" QToolButton { border-radius: 8px; }"_s);
    auto* content = new QHBoxLayout(button);
    content->setContentsMargins(10, 0, 10, 0);
    content->setSpacing(10);
    auto* iconLabel = new QLabel(button);
    iconLabel->setPixmap(symbolic(icon, this).pixmap(IconSize, IconSize));
    auto* nameLabel = new QLabel(name, button);
    nameLabel->setTextFormat(Qt::PlainText);
    auto* stateLabel = new QLabel(state, button);
    stateLabel->setStyleSheet(u"color: %1;"_s.arg(tde::theme::colors().dimText.name()));
    for (QLabel* label : {iconLabel, nameLabel, stateLabel})
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    content->addWidget(iconLabel);
    content->addWidget(nameLabel, 1);
    content->addWidget(stateLabel);
    return button;
}

void QuickSettings::syncWifi()
{
    // The networks in range, the strongest first, connecting to one or letting it go.
    std::vector<shell::WifiNetwork> networks;
    if (m_wifi.isAvailable() && m_wifi.isEnabled()) {
        for (const shell::WifiNetwork& network : m_wifi.networks()) {
            if (network.strength >= 0 && networks.size() < 6)
                networks.push_back(network);
        }
    }
    m_networks->setVisible(!networks.empty());
    if (networks == m_shownNetworks)
        return;
    m_shownNetworks = std::move(networks);
    shell::clearLayout(*m_networkList);
    for (const shell::WifiNetwork& network : m_shownNetworks) {
        const QString state = network.active ? u"Connected"_s : network.needsPassword() ? u"Secured"_s : QString();
        QToolButton* button = listButton(m_networks, shell::wifiIconName(network.strength), network.ssid, state);
        button->setToolTip(network.active ? u"Disconnect"_s : u"Connect"_s);
        connect(button, &QToolButton::clicked, this, [this, ssid = network.ssid, active = network.active] {
            if (active)
                m_wifi.disconnect();
            else
                emit wifiNetworkChosen(ssid);
        });
        m_networkList->addWidget(button);
    }
}

} // namespace hermes
