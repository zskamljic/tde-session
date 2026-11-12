#include "Pages.hpp"

#include <Applications.hpp>
#include <DesktopSettings.hpp>

#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

// Keys as printed on them, in a box each.
QLabel* keys(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(u"Keys"_s);
    return label;
}

QSpinBox* milliseconds(int value, QWidget* parent)
{
    auto* box = new QSpinBox(parent);
    box->setRange(0, 2000);
    box->setSingleStep(50);
    box->setSuffix(u" ms"_s);
    box->setSpecialValueText(u"Off"_s);
    box->setValue(value);
    return box;
}

} // namespace

WindowsPage::WindowsPage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    using Style = shell::SessionConfig::Switcher::Style;
    auto& config = settings.config;

    auto* style = new QComboBox(this);
    style->addItem(u"Pictures of the windows"_s, int(Style::Previews));
    style->addItem(u"Application icons"_s, int(Style::Icons));
    style->setCurrentIndex(style->findData(int(config.switcher.style)));
    connect(style, &QComboBox::activated, this, [&settings, style] {
        settings.config.switcher.style = Style(style->currentData().toInt());
        settings.save();
    });
    Group* switching = addGroup(u"Switching Windows"_s);
    switching->addRow(u"Alt+Tab shows"_s, u"What the switcher shows of every window"_s, style);

    // As Windows 7 had its button at the end of the taskbar, here at the start of the bar.
    using Corner = shell::SessionConfig::Windows::Corner;
    auto* corner = new QComboBox(this);
    corner->addItem(u"Opens the overview"_s, int(Corner::Overview));
    corner->addItem(u"Shows the desktop"_s, int(Corner::Desktop));
    corner->addItem(u"Does nothing"_s, int(Corner::Off));
    corner->setCurrentIndex(corner->findData(int(config.windows.corner)));
    connect(corner, &QComboBox::activated, this, [&settings, corner] {
        settings.config.windows.corner = Corner(corner->currentData().toInt());
        settings.save();
    });
    // The side of the title bars' buttons, which TDE's programs, GTK's and Atlas follow.
    m_buttonSide = new QComboBox(this);
    m_buttonSide->addItem(u"Right"_s, u"right"_s);
    m_buttonSide->addItem(u"Left"_s, u"left"_s);
    desktopChanged(tde::loadDesktopConfig());
    connect(m_buttonSide, &QComboBox::activated, this, [this] {
        if (!shell::setDesktopString(
                tde::desktopConfigPath(), u"window_buttons"_s, u"position"_s, m_buttonSide->currentData().toString()))
            qWarning("tde-daedalus: cannot write %s", qPrintable(tde::desktopConfigPath()));
    });
    Group* titleBars = addGroup(u"Title Bars"_s);
    titleBars->addRow(u"Buttons"_s, u"The end of the title bar that has the window buttons"_s, m_buttonSide);

    auto* minimized = new Switch(this);
    minimized->setChecked(config.windows.minimizedInOverview);
    connect(minimized, &Switch::toggled, this, [&settings](bool on) {
        settings.config.windows.minimizedInOverview = on;
        settings.save();
    });

    Group* desktop = addGroup(u"Desktop"_s);
    desktop->addRow(u"Hot corner"_s,
        u"Pushing the pointer into the top left corner, or clicking the button there; doing it again undoes it"_s,
        corner);
    desktop->addRow(u"Minimized windows in the overview"_s,
        u"Off, a minimized window comes back only from its button in the bar"_s, minimized);

    Group* animations = addGroup(u"Animations"_s);
    const auto duration = [&](const QString& title, const QString& subtitle, int& value) {
        QSpinBox* box = milliseconds(value, this);
        connect(box, &QSpinBox::valueChanged, this, [&settings, &value](int changed) {
            value = changed;
            settings.save();
        });
        animations->addRow(title, subtitle, box);
    };
    duration(u"Overview"_s, u"Windows moving in and out of the overview"_s, config.animations.overview);
    duration(u"Flip 3D"_s, u"Windows moving into the stack and back"_s, config.animations.flip);
    duration(u"Flip 3D turning"_s, u"The next window coming to the front"_s, config.animations.flipStep);
    duration(u"Tiling"_s, u"Windows growing to fill the screen or half of it"_s, config.animations.windows);

    Group* shortcuts = addGroup(u"Keyboard Shortcuts"_s);
    shortcuts->addRow(u"Switch windows"_s, {}, keys(u"Alt+Tab"_s, this));
    shortcuts->addRow(u"Switch windows of an application"_s, {}, keys(u"Alt+`"_s, this));
    shortcuts->addRow(u"Flip through windows in 3D"_s, {}, keys(u"Super+Tab"_s, this));
    shortcuts->addRow(u"Show all windows"_s, {}, keys(u"Super"_s, this));
    shortcuts->addNote(u"Shift goes backwards; letting go of Alt or Super switches to the window picked."_s);
}

void WindowsPage::desktopChanged(const tde::DesktopConfig& config)
{
    m_buttonSide->setCurrentIndex(config.windowButtons.side == tde::ButtonSide::Left ? 1 : 0);
}

DateTimePage::DateTimePage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    auto* seconds = new Switch(this);
    seconds->setChecked(settings.config.clock.seconds);
    connect(seconds, &Switch::toggled, this, [&settings](bool on) {
        settings.config.clock.seconds = on;
        settings.save();
    });
    Group* clock = addGroup(u"Clock"_s);
    clock->addRow(u"Seconds"_s, u"In the clock of the bar and of the lock screen"_s, seconds);
}

NotificationsPage::NotificationsPage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    auto* quiet = new Switch(this);
    quiet->setChecked(settings.config.notifications.quiet);
    connect(quiet, &Switch::toggled, this, [&settings](bool on) {
        settings.config.notifications.quiet = on;
        settings.save();
    });
    Group* general = addGroup(u"Notifications"_s);
    general->addRow(u"Do Not Disturb"_s,
        u"Only critical notifications show; the rest go to the list behind the clock, silently"_s, quiet);

    // Those that sent some, and those set otherwise before, by the names their desktop entries
    // give them where there are such.
    std::vector<shell::NotificationSender> senders = shell::loadNotificationSenders();
    for (const auto& app : settings.config.notifications.apps) {
        if (!std::ranges::contains(senders, app.id, &shell::NotificationSender::id))
            senders.push_back({app.id, app.id});
    }
    const auto apps = shell::loadApplications(qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':'));
    const auto nameOf = [&](const shell::NotificationSender& sender) {
        if (const shell::Application* app = shell::findApplication(apps, sender.id))
            return app->name;
        return sender.name.isEmpty() ? sender.id : sender.name;
    };
    std::ranges::sort(
        senders, [&](const auto& a, const auto& b) { return QString::localeAwareCompare(nameOf(a), nameOf(b)) < 0; });

    Group* group = addGroup(u"Applications"_s);
    for (const shell::NotificationSender& sender : senders) {
        auto* choice = new QComboBox(this);
        choice->addItems({u"Shown, with sound"_s, u"Shown, silently"_s, u"Not shown"_s});
        const auto app = settings.config.notifications.find(sender.id);
        choice->setCurrentIndex(!app.show ? 2 : !app.sound ? 1 : 0);
        connect(choice, &QComboBox::currentIndexChanged, this, [&settings, id = sender.id](int index) {
            settings.config.notifications.keep({.id = id, .show = index != 2, .sound = index == 0});
            settings.save();
        });
        group->addRow(nameOf(sender), {}, choice);
    }
    if (senders.empty())
        group->addRow(u"No notifications yet"_s, u"Applications show up here once they send one"_s);
    group->addNote(u"Programs that play a sound of their own with a notification are not given a second one."_s);
}

} // namespace daedalus
