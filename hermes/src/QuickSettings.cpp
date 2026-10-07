#include "QuickSettings.hpp"

#include "Audio.hpp"
#include "SystemStatus.hpp"

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
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

QToolButton* flatButton(QWidget* parent)
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

QuickSettings::QuickSettings(Audio& audio, Brightness& brightness, Network& network, Battery& battery, QWidget* parent)
    : QWidget(parent)
    , m_audio(audio)
    , m_brightness(brightness)
    , m_network(network)
    , m_battery(battery)
{
    setFixedWidth(340);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    QHBoxLayout* line = nullptr;
    m_volumeRow = row(this, line);
    m_mute = flatButton(m_volumeRow);
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

    m_networkRow = row(this, line);
    m_networkIcon = new QLabel(m_networkRow);
    m_networkIcon->setFixedSize(32, 32);
    m_networkIcon->setAlignment(Qt::AlignCenter);
    m_networkText = new QLabel(m_networkRow);
    m_networkText->setTextFormat(Qt::PlainText);
    m_wifi = flatButton(m_networkRow);
    m_wifi->setCheckable(true);
    m_wifi->setToolTip(u"Wi-Fi"_s);
    m_wifi->setIcon(symbolic(u"network-wireless-symbolic"_s, this));
    connect(m_wifi, &QToolButton::toggled, this, [this](bool on) { m_network.setWifiEnabled(on); });
    line->addWidget(m_networkIcon);
    line->addWidget(m_networkText, 1);
    line->addWidget(m_wifi);
    layout->addWidget(m_networkRow);

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
    connect(&m_battery, &Battery::changed, this, &QuickSettings::sync);
    sync();
}

QToolButton* QuickSettings::actionButton(const QString& icon, const QString& name, void (QuickSettings::*signal)())
{
    QToolButton* button = flatButton(this);
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
    connect(button, &QToolButton::clicked, this, signal);
    return button;
}

void QuickSettings::refresh()
{
    m_brightness.refresh();
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
    m_networkText->setText(m_network.description());
    m_wifi->setVisible(m_network.hasWifi());
    {
        const QSignalBlocker blocker(m_wifi);
        m_wifi->setChecked(m_network.isWifiEnabled());
    }

    m_batteryRow->setVisible(m_battery.isPresent());
    m_batteryIcon->setPixmap(symbolic(m_battery.iconName(), this).pixmap(IconSize, IconSize));
    const QString time = m_battery.timeText();
    m_batteryText->setText(
        time.isEmpty() ? u"%1%"_s.arg(m_battery.percentage()) : u"%1%  ·  %2"_s.arg(m_battery.percentage()).arg(time));
    adjustSize();
}

} // namespace hermes
