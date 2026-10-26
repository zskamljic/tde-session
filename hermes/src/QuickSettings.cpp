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
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int IconSize = 16;
constexpr int Steps = 100;
constexpr int ToggleHeight = 44;

// Plain text cut short with an ellipsis where it does not fit, instead of widening what holds it.
class ElidedLabel : public QLabel {
public:
    ElidedLabel(const QString& text, QWidget* parent)
        : QLabel(text, parent)
    {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
            fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

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

// A level from 0 to Steps, with a round knob on a thin track, filled in up to it, as the
// theme draws sliders.
QSlider* slider(QWidget* parent)
{
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(0, Steps);
    slider->setMinimumHeight(24);
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

    // At the top, the battery and round buttons for the settings, locking and the power menu.
    QHBoxLayout* line = nullptr;
    QWidget* top = row(this, line);
    m_batteryIcon = new QLabel(top);
    m_batteryIcon->setFixedSize(IconSize, 32);
    m_batteryIcon->setAlignment(Qt::AlignCenter);
    m_batteryText = new QLabel(top);
    m_batteryText->setTextFormat(Qt::PlainText);
    line->addSpacing(8);
    line->addWidget(m_batteryIcon);
    line->addWidget(m_batteryText);
    line->addStretch();
    QToolButton* settings = topButton(u"preferences-system-symbolic"_s, u"Settings"_s);
    connect(settings, &QToolButton::clicked, this, [this] { emit settingsRequested({}); });
    QToolButton* lock = topButton(u"system-lock-screen-symbolic"_s, u"Lock the Screen"_s);
    connect(lock, &QToolButton::clicked, this, &QuickSettings::lockRequested);
    m_power = topButton(u"system-shutdown-symbolic"_s, u"Power Off or Log Out"_s);
    m_power->setCheckable(true);
    line->addWidget(settings);
    line->addWidget(lock);
    line->addWidget(m_power);
    layout->addWidget(top);

    // Those that ask first end in an ellipsis.
    QVBoxLayout* column = nullptr;
    m_powerMenu = menuCard(u"system-shutdown-symbolic"_s, u"Power"_s, column);
    column->addWidget(
        actionButton(m_powerMenu, u"media-playback-pause-symbolic"_s, u"Suspend"_s, &QuickSettings::suspendRequested));
    column->addWidget(
        actionButton(m_powerMenu, u"system-reboot-symbolic"_s, u"Restart…"_s, &QuickSettings::restartRequested));
    column->addWidget(
        actionButton(m_powerMenu, u"system-shutdown-symbolic"_s, u"Shut Down…"_s, &QuickSettings::shutDownRequested));
    column->addWidget(
        actionButton(m_powerMenu, u"system-log-out-symbolic"_s, u"Log Out…"_s, &QuickSettings::logOutRequested));
    layout->addWidget(m_powerMenu);
    connect(m_power, &QToolButton::clicked, this, [this] { toggleMenu(m_powerMenu); });

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

    // Two by two, those the machine has, laid out by layOutToggles().
    auto* toggles = new QWidget(this);
    m_grid = new QGridLayout(toggles);
    m_grid->setContentsMargins(0, 2, 0, 2);
    m_grid->setSpacing(8);
    m_grid->setColumnStretch(0, 1);
    m_grid->setColumnStretch(1, 1);

    menuToggle(m_wifiToggle, u"Wi-Fi"_s, u"network"_s, u"Network Settings"_s);
    connect(m_wifiToggle.main, &QToolButton::clicked, this, [this](bool on) { m_network.setWifiEnabled(on); });
    m_wired = toggle(QString());
    m_wired->setCheckable(false);
    m_wired->setToolTip(u"Network Settings"_s);
    connect(m_wired, &QToolButton::clicked, this, [this] { emit settingsRequested(u"network"_s); });
    menuToggle(m_bluetoothToggle, u"Bluetooth"_s, u"bluetooth"_s, u"Bluetooth Settings"_s);
    connect(m_bluetoothToggle.main, &QToolButton::clicked, this, [this](bool on) { m_bluetooth.setPowered(on); });

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
    layout->addWidget(toggles);

    connect(&m_audio, &Audio::changed, this, &QuickSettings::sync);
    connect(&m_brightness, &Brightness::changed, this, &QuickSettings::sync);
    connect(&m_network, &Network::changed, this, &QuickSettings::sync);
    connect(&m_bluetooth, &shell::Bluetooth::changed, this, &QuickSettings::sync);
    connect(&m_wifi, &shell::Wifi::changed, this, &QuickSettings::sync);
    // What went wrong opens the menu it is about and shows there, for a while.
    const auto showProblem = [this](QString QuickSettings::* problem, MenuToggle QuickSettings::* toggle) {
        return [this, problem, toggle](const QString& message) {
            this->*problem = message;
            if (m_openMenu != (this->*toggle).menu)
                toggleMenu((this->*toggle).menu);
            sync();
            QTimer::singleShot(6000, this, [this, problem, message] {
                if (this->*problem == message) {
                    (this->*problem).clear();
                    sync();
                }
            });
        };
    };
    connect(&m_bluetooth, &shell::Bluetooth::failed, this,
        showProblem(&QuickSettings::m_bluetoothProblem, &QuickSettings::m_bluetoothToggle));
    connect(
        &m_wifi, &shell::Wifi::failed, this, showProblem(&QuickSettings::m_wifiProblem, &QuickSettings::m_wifiToggle));
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
    button->setFixedHeight(ToggleHeight);
    button->setStyleSheet(u"QToolButton { background: %1; border: none; border-radius: 20px; padding: 0 14px;"
                          " text-align: left; }"
                          " QToolButton:hover { background: %2; }"
                          " QToolButton:checked { background: %3; color: %4; }"
                          " QToolButton:checked:hover { background: %5; }"_s.arg(colors.pressed.name(QColor::HexArgb),
                              colors.hover.name(QColor::HexArgb), colors.accent.name(), colors.accentText.name(),
                              colors.accent.lighter(115).name()));
    return button;
}

void QuickSettings::menuToggle(
    MenuToggle& toggle, const QString& name, const QString& settingsPage, const QString& settingsName)
{
    const auto& colors = tde::theme::colors();
    // The two halves of one pill, a hair apart.
    const QString look
        = u"QToolButton { background: %1; border: none; %6 }"
          " QToolButton:hover { background: %2; }"
          " QToolButton:checked { background: %3; }"
          " QToolButton:checked:hover { background: %4; }"_s.arg(colors.pressed.name(QColor::HexArgb),
              colors.hover.name(QColor::HexArgb), colors.accent.name(), colors.accent.lighter(115).name());
    toggle.widget = new QWidget(this);
    auto* halves = new QHBoxLayout(toggle.widget);
    halves->setContentsMargins(0, 0, 0, 0);
    halves->setSpacing(1);

    toggle.main = new QToolButton(toggle.widget);
    toggle.main->setCheckable(true);
    toggle.main->setAccessibleName(name);
    toggle.main->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggle.main->setFixedHeight(ToggleHeight);
    toggle.main->setStyleSheet(look.arg(u"border-top-left-radius: 20px; border-bottom-left-radius: 20px;"_s));
    auto* content = new QHBoxLayout(toggle.main);
    content->setContentsMargins(14, 0, 6, 0);
    content->setSpacing(10);
    toggle.icon = new QLabel(toggle.main);
    toggle.icon->setFixedSize(IconSize, IconSize);
    auto* words = new QVBoxLayout;
    words->setContentsMargins(0, 0, 0, 0);
    words->setSpacing(0);
    toggle.title = new ElidedLabel(name, toggle.main);
    QFont bold = toggle.title->font();
    bold.setBold(true);
    toggle.title->setFont(bold);
    toggle.subtitle = new ElidedLabel(QString(), toggle.main);
    QFont small = toggle.subtitle->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    toggle.subtitle->setFont(small);
    words->addStretch();
    words->addWidget(toggle.title);
    words->addWidget(toggle.subtitle);
    words->addStretch();
    for (QLabel* label : {toggle.icon, toggle.title, toggle.subtitle})
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    content->addWidget(toggle.icon);
    content->addLayout(words, 1);

    toggle.arrow = new QToolButton(toggle.widget);
    toggle.arrow->setCheckable(true);
    toggle.arrow->setFixedSize(34, ToggleHeight);
    toggle.arrow->setIconSize(QSize(IconSize, IconSize));
    toggle.arrow->setToolTip(name + u" Menu"_s);
    toggle.arrow->setStyleSheet(
        look.arg(u"border-top-right-radius: 20px; border-bottom-right-radius: 20px; padding-right: 4px;"_s));
    halves->addWidget(toggle.main, 1);
    halves->addWidget(toggle.arrow);

    QVBoxLayout* column = nullptr;
    toggle.menu = menuCard(QString(), name, column);
    toggle.problem = new QLabel(toggle.menu);
    toggle.problem->setTextFormat(Qt::PlainText);
    toggle.problem->setWordWrap(true);
    toggle.problem->setContentsMargins(10, 0, 10, 4);
    toggle.problem->setStyleSheet(u"color: %1;"_s.arg(colors.error.name()));
    toggle.empty = new QLabel(toggle.menu);
    toggle.empty->setTextFormat(Qt::PlainText);
    toggle.empty->setContentsMargins(10, 4, 10, 4);
    toggle.empty->setStyleSheet(u"color: %1;"_s.arg(colors.dimText.name()));
    toggle.list = new QVBoxLayout;
    toggle.list->setContentsMargins(0, 0, 0, 0);
    toggle.list->setSpacing(2);
    auto* separator = new QWidget(toggle.menu);
    separator->setFixedHeight(1);
    separator->setStyleSheet(u"background: %1;"_s.arg(colors.border.name()));
    QToolButton* settings = actionButton(toggle.menu, u"preferences-system-symbolic"_s, settingsName, nullptr);
    connect(settings, &QToolButton::clicked, this, [this, settingsPage] { emit settingsRequested(settingsPage); });
    column->addWidget(toggle.problem);
    column->addWidget(toggle.empty);
    column->addLayout(toggle.list);
    column->addSpacing(2);
    column->addWidget(separator);
    column->addWidget(settings);

    QWidget* menu = toggle.menu;
    connect(toggle.arrow, &QToolButton::clicked, this, [this, menu] { toggleMenu(menu); });
}

void QuickSettings::paintMenuToggle(MenuToggle& toggle, const QString& icon, const QString& subtitle, bool on)
{
    const auto& colors = tde::theme::colors();
    const QColor text = on ? colors.accentText : colors.text;
    {
        const QSignalBlocker blocker(toggle.main);
        toggle.main->setChecked(on);
    }
    toggle.arrow->setChecked(on);
    toggle.icon->setPixmap(
        shell::tintedIcon(icon, QSize(IconSize, IconSize), devicePixelRatioF(), text).pixmap(IconSize, IconSize));
    toggle.title->setStyleSheet(u"color: %1;"_s.arg(text.name()));
    toggle.subtitle->setText(subtitle);
    toggle.subtitle->setStyleSheet(u"color: %1;"_s.arg(on ? colors.accentText.name() : colors.dimText.name()));
    toggle.main->setToolTip(subtitle);
    const bool open = m_openMenu == toggle.menu;
    toggle.arrow->setIcon(shell::tintedIcon(
        open ? u"go-down-symbolic"_s : u"go-next-symbolic"_s, QSize(IconSize, IconSize), devicePixelRatioF(), text));
}

QWidget* QuickSettings::menuCard(const QString& icon, const QString& heading, QVBoxLayout*& content)
{
    auto* card = new QWidget(this);
    card->setObjectName(u"menuCard"_s);
    card->setAttribute(Qt::WA_StyledBackground);
    card->setStyleSheet(u"#menuCard { background: %1; border-radius: 14px; }"_s.arg(
        tde::theme::colors().pressed.name(QColor::HexArgb)));
    content = new QVBoxLayout(card);
    content->setContentsMargins(6, 8, 6, 6);
    content->setSpacing(2);
    QHBoxLayout* line = nullptr;
    QWidget* title = row(card, line);
    line->setContentsMargins(10, 2, 10, 6);
    if (!icon.isEmpty()) {
        auto* image = new QLabel(title);
        image->setPixmap(symbolic(icon, this).pixmap(IconSize, IconSize));
        line->addWidget(image);
    }
    auto* words = new QLabel(heading, title);
    QFont bold = words->font();
    bold.setBold(true);
    words->setFont(bold);
    line->addWidget(words, 1);
    content->addWidget(title);
    card->hide();
    return card;
}

void QuickSettings::toggleMenu(QWidget* menu)
{
    m_openMenu = m_openMenu == menu ? nullptr : menu;
    // In place first: shown before, a menu would show where it was last.
    layOutToggles();
    for (QWidget* each : {m_powerMenu, m_wifiToggle.menu, m_bluetoothToggle.menu})
        each->setVisible(each == m_openMenu);
    m_power->setChecked(m_openMenu == m_powerMenu);
    sync();
}

void QuickSettings::layOutToggles()
{
    while (m_grid->count() > 0)
        delete m_grid->takeAt(0);
    struct Placed {
        QWidget* toggle;
        bool shown;
        QWidget* menu = nullptr;
    };
    const bool network = m_network.isAvailable();
    const Placed all[] {
        {m_wifiToggle.widget, network && m_network.hasWifi(), m_wifiToggle.menu},
        {m_wired, network && !m_network.hasWifi()},
        {m_bluetoothToggle.widget, m_bluetooth.isAvailable(), m_bluetoothToggle.menu},
        {m_darkStyle, true},
        {m_quiet, true},
        {m_powerMode, m_profiles.isAvailable() && !m_profiles.offered().isEmpty()},
        {m_screenshot, true},
    };
    int line = 0;
    int column = 0;
    QWidget* menu = nullptr; // under the row being filled
    for (const Placed& each : all) {
        each.toggle->setVisible(each.shown);
        // The menu of one gone closes.
        if (each.menu && !each.shown && m_openMenu == each.menu) {
            m_openMenu = nullptr;
            each.menu->hide();
        }
        if (!each.shown)
            continue;
        m_grid->addWidget(each.toggle, line, column);
        if (each.menu && each.menu == m_openMenu)
            menu = each.menu;
        if (++column == 2) {
            column = 0;
            ++line;
            if (std::exchange(menu, nullptr))
                m_grid->addWidget(m_openMenu, line++, 0, 1, 2);
        }
    }
    if (menu)
        m_grid->addWidget(menu, line + 1, 0, 1, 2);
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
    m_powerMode->setText(shell::powerProfileName(m_profiles.active()));
    setIcon(m_powerMode, shell::powerProfileIcon(m_profiles.active()));
    setIcon(m_screenshot, u"camera-photo-symbolic"_s);
    m_wired->setText(m_network.description());
    setIcon(m_wired, m_network.iconName());
    layOutToggles();
}

QToolButton* QuickSettings::actionButton(
    QWidget* parent, const QString& icon, const QString& name, void (QuickSettings::*signal)())
{
    QToolButton* button = roundButton(parent);
    // As wide as the panel, the icon before the words with room between them.
    button->setMinimumSize(0, 36);
    button->setMaximumSize(QWIDGETSIZE_MAX, 36);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(button->styleSheet() + u" QToolButton { border-radius: 8px; }"_s);
    button->setAccessibleName(name);
    auto* content = new QHBoxLayout(button);
    content->setContentsMargins(10, 0, 10, 0);
    content->setSpacing(10);
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

QToolButton* QuickSettings::topButton(const QString& icon, const QString& name)
{
    QToolButton* button = roundButton(this);
    button->setFixedSize(36, 36);
    button->setStyleSheet(button->styleSheet().replace(u"16px"_s, u"18px"_s));
    button->setToolTip(name);
    button->setAccessibleName(name);
    button->setIcon(symbolic(icon, this));
    return button;
}

bool QuickSettings::event(QEvent* event)
{
    const bool handled = QWidget::event(event);
    // Rows that came or went while the menu was closed are measured only once it asks for the
    // layout again; the panel then takes the size they need, as it would have at once.
    if (event->type() == QEvent::LayoutRequest && size() != sizeHint())
        adjustSize();
    return handled;
}

void QuickSettings::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit resized();
}

void QuickSettings::refresh()
{
    if (m_openMenu)
        toggleMenu(m_openMenu);
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

    syncWifi();
    syncBluetooth();
    syncToggles();

    const bool battery = m_battery.isPresent();
    m_batteryIcon->setVisible(battery);
    m_batteryText->setVisible(battery);
    m_batteryIcon->setPixmap(symbolic(m_battery.iconName(), this).pixmap(IconSize, IconSize));
    const QString time = m_battery.timeText();
    m_batteryText->setText(
        time.isEmpty() ? u"%1%"_s.arg(m_battery.percentage()) : u"%1%  ·  %2"_s.arg(m_battery.percentage()).arg(time));
    adjustSize();
}

void QuickSettings::syncBluetooth()
{
    const bool on = m_bluetooth.isPowered();
    const QString connected = m_bluetooth.connectedNames();
    paintMenuToggle(m_bluetoothToggle, on ? u"bluetooth-active-symbolic"_s : u"bluetooth-disabled-symbolic"_s,
        !on ? u"Off"_s
            : connected.isEmpty() ? u"On"_s
                                  : connected,
        on);
    m_bluetoothToggle.problem->setText(m_bluetoothProblem);
    m_bluetoothToggle.problem->setVisible(!m_bluetoothProblem.isEmpty());

    // A button for each device paired, connecting it or letting it go.
    auto devices = on ? m_bluetooth.paired() : std::vector<shell::BluetoothDevice> {};
    m_bluetoothToggle.empty->setText(on ? u"No devices paired"_s : u"Bluetooth is off"_s);
    m_bluetoothToggle.empty->setVisible(devices.empty());
    if (devices == m_shownDevices)
        return;
    m_shownDevices = std::move(devices);
    shell::clearLayout(*m_bluetoothToggle.list);
    for (const shell::BluetoothDevice& device : m_shownDevices) {
        QToolButton* button = listButton(m_bluetoothToggle.menu,
            device.icon.isEmpty() ? u"bluetooth-active-symbolic"_s : device.icon + u"-symbolic"_s, device.name,
            device.connected ? u"Connected"_s : QString());
        button->setToolTip(device.connected ? u"Disconnect"_s : u"Connect"_s);
        connect(button, &QToolButton::clicked, this, [this, path = device.path, connected = device.connected] {
            if (connected)
                m_bluetooth.disconnectDevice(path);
            else
                m_bluetooth.connectDevice(path);
        });
        m_bluetoothToggle.list->addWidget(button);
    }
}

QToolButton* QuickSettings::listButton(QWidget* parent, const QString& icon, const QString& name, const QString& state)
{
    QToolButton* button = roundButton(parent);
    button->setMinimumSize(0, 32);
    button->setMaximumSize(QWIDGETSIZE_MAX, 32);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(button->styleSheet() + u" QToolButton { border-radius: 8px; }"_s);
    auto* content = new QHBoxLayout(button);
    content->setContentsMargins(10, 0, 10, 0);
    content->setSpacing(10);
    auto* iconLabel = new QLabel(button);
    iconLabel->setPixmap(symbolic(icon, this).pixmap(IconSize, IconSize));
    auto* nameLabel = new ElidedLabel(name, button);
    auto* stateLabel = new QLabel(state, button);
    stateLabel->setStyleSheet(u"color: %1;"_s.arg(tde::theme::colors().dimText.name()));
    for (QLabel* label : {iconLabel, static_cast<QLabel*>(nameLabel), stateLabel})
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    content->addWidget(iconLabel);
    content->addWidget(nameLabel, 1);
    content->addWidget(stateLabel);
    return button;
}

void QuickSettings::syncWifi()
{
    const bool on = m_network.isWifiEnabled();
    paintMenuToggle(m_wifiToggle, on ? m_network.iconName() : u"network-wireless-disabled-symbolic"_s,
        on ? m_network.description() : u"Off"_s, on);
    m_wifiToggle.problem->setText(m_wifiProblem);
    m_wifiToggle.problem->setVisible(!m_wifiProblem.isEmpty());

    // The networks in range, the strongest first, connecting to one or letting it go.
    std::vector<shell::WifiNetwork> networks;
    if (m_wifi.isAvailable() && m_wifi.isEnabled()) {
        for (const shell::WifiNetwork& network : m_wifi.networks()) {
            if (network.strength >= 0 && networks.size() < 8)
                networks.push_back(network);
        }
    }
    m_wifiToggle.empty->setText(on ? u"No networks in range"_s : u"Wi-Fi is off"_s);
    m_wifiToggle.empty->setVisible(networks.empty());
    if (networks == m_shownNetworks)
        return;
    m_shownNetworks = std::move(networks);
    shell::clearLayout(*m_wifiToggle.list);
    for (const shell::WifiNetwork& network : m_shownNetworks) {
        const QString state = network.active ? u"Connected"_s : network.needsPassword() ? u"Secured"_s : QString();
        QToolButton* button = listButton(m_wifiToggle.menu, shell::wifiIconName(network.strength), network.ssid, state);
        button->setToolTip(network.active ? u"Disconnect"_s : u"Connect"_s);
        connect(button, &QToolButton::clicked, this, [this, ssid = network.ssid, active = network.active] {
            if (active)
                m_wifi.disconnect();
            else
                emit wifiNetworkChosen(ssid);
        });
        m_wifiToggle.list->addWidget(button);
    }
}

} // namespace hermes
