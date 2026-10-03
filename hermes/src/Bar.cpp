#include "Bar.hpp"

#include "Groups.hpp"
#include "Locking.hpp"
#include "NotificationViews.hpp"
#include "Osd.hpp"
#include "PolkitAgent.hpp"
#include "QuickSettings.hpp"
#include "Taskbar.hpp"
#include "Tray.hpp"

#include <Icons.hpp>
#include <tde/ConfigWatcher.hpp>
#include <tde/DesktopConfig.hpp>
#include <tde/Dialog.hpp>
#include <tde/Theme.hpp>

#include <LayerShellQt/Window>

#include <QCalendarWidget>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QTextCharFormat>
#include <QToolButton>
#include <QWidgetAction>

#include <functional>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int Height = 34;
constexpr int Spacing = 6;

// The look of the bar's buttons: flat, a rounded highlight under the pointer and while open.
QString buttonStyle(const QString& extra)
{
    const auto& colors = tde::theme::colors();
    return u"QToolButton { background: transparent; border: none; border-radius: %1px; %2 }"
           " QToolButton:hover { background: %3; }"
           " QToolButton:pressed, QToolButton:open { background: %4; }"
           " QToolButton::menu-indicator { image: none; width: 0; }"_s.arg(tde::theme::radius())
               .arg(extra, colors.hover.name(QColor::HexArgb), colors.pressed.name(QColor::HexArgb));
}

// Asks logind for `method` without waiting for it, as polkit may ask for a password first.
void callLogind(
    const QString& path, const QString& interface, const QString& method, const QVariantList& arguments = {})
{
    auto message = QDBusMessage::createMethodCall(u"org.freedesktop.login1"_s, path, interface, method);
    message.setArguments(arguments);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [method](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (call->isError())
            qWarning("tde-hermes: %s failed: %s", qPrintable(method), qPrintable(call->error().message()));
    });
}

// logind lets the user of the active session suspend, restart and shut down.
void power(const QString& method)
{
    // Interactive: polkit may ask for a password when others are logged in.
    callLogind(u"/org/freedesktop/login1"_s, u"org.freedesktop.login1.Manager"_s, method, {true});
}

void logOut()
{
    // Ending the session ends everything in it, the compositor too, and the login screen
    // comes back.
    callLogind(u"/org/freedesktop/login1/session/auto"_s, u"org.freedesktop.login1.Session"_s, u"Terminate"_s);
}

} // namespace

Bar::Bar(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(u"Bar"_s);
    setFixedHeight(Height);
    // The bar never takes the keyboard, so it never counts as active; tooltips show anyway.
    setAttribute(Qt::WA_AlwaysShowToolTips);

    // Along the top edge of the screen, which windows keep clear of.
    create();
    if (auto* layer = LayerShellQt::Window::get(windowHandle())) {
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layer->setExclusiveZone(Height);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope(u"tde-hermes"_s);
    }

    const auto& colors = tde::theme::colors();
    m_taskbar = new Taskbar(m_windows, this);

    m_clock = new QToolButton(this);
    m_clock->setAutoRaise(true);
    m_clock->setPopupMode(QToolButton::InstantPopup);
    m_clock->setStyleSheet(buttonStyle(u"font-weight: bold; padding: 0 10px;"_s));
    // Behind the clock: the notifications that were missed, and the calendar.
    auto* calendarMenu = new QMenu(m_clock);
    auto* dropdown = new QWidget(calendarMenu);
    auto* dropdownLayout = new QHBoxLayout(dropdown);
    dropdownLayout->setContentsMargins(0, 0, 0, 0);
    auto* list = new NotificationList(m_notifications, dropdown);
    connect(list, &NotificationList::handled, calendarMenu, &QMenu::close);
    connect(list, &NotificationList::actionPicked, this, &Bar::pickAction);
    dropdownLayout->addWidget(list);
    auto* calendar = new QCalendarWidget(dropdown);
    dropdownLayout->addWidget(calendar, 0, Qt::AlignTop);
    calendar->setGridVisible(false);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    // Weekends in the text colour too, not in Qt's red.
    QTextCharFormat weekend;
    weekend.setForeground(colors.text);
    calendar->setWeekdayTextFormat(Qt::Saturday, weekend);
    calendar->setWeekdayTextFormat(Qt::Sunday, weekend);
    auto* calendarAction = new QWidgetAction(calendarMenu);
    calendarAction->setDefaultWidget(dropdown);
    calendarMenu->addAction(calendarAction);
    connect(calendarMenu, &QMenu::aboutToShow, calendar, [calendar] {
        calendar->setSelectedDate(QDate::currentDate());
        calendar->showToday();
    });
    m_clock->setMenu(calendarMenu);

    m_tray = new Tray(this);
    m_locking = new Locking(this);
    m_polkit = new PolkitAgent(this);
    if (!m_polkit->start())
        qWarning("tde-hermes: another program asks for passwords for polkit already");
    m_locking->setIdleMinutes(tde::desktop().lock.after);
    QDBusConnection::sessionBus().registerObject(QString::fromLatin1(BusPath),
        new MediaKeys(m_audio, m_brightness, this), QDBusConnection::ExportScriptableSlots);
    // Edits to the desktop's config take effect at once.
    auto* watcher = new tde::ConfigWatcher({tde::desktopConfigPath()}, this);
    connect(watcher, &tde::ConfigWatcher::changed, this, [this] {
        tde::setDesktop(tde::loadDesktopConfig());
        m_locking->setIdleMinutes(tde::desktop().lock.after);
    });

    if (m_notifications.start()) {
        m_banners = std::make_unique<Banners>(m_notifications);
        connect(m_banners.get(), &Banners::actionPicked, this, &Bar::pickAction);
    } else {
        qWarning("tde-hermes: another program shows the notifications already");
    }
    connect(&m_notifications, &NotificationServer::arrived, this, [this](uint id) {
        if (const Notification* notification = m_notifications.find(id))
            m_sounds.play(*notification);
    });
    connect(&m_notifications, &NotificationServer::changed, this, &Bar::updateClock);

    m_system = new QToolButton(this);
    m_system->setAutoRaise(true);
    m_system->setToolTip(u"Sound, Network, Battery and Power"_s);
    m_system->setPopupMode(QToolButton::InstantPopup);
    m_system->setStyleSheet(buttonStyle(u"padding: 0 10px;"_s));
    m_system->setMenu(createQuickSettings());
    connect(&m_audio, &Audio::changed, this, &Bar::updateStatusIcon);
    connect(&m_network, &Network::changed, this, &Bar::updateStatusIcon);
    connect(&m_battery, &Battery::changed, this, &Bar::updateStatusIcon);
    updateStatusIcon();

    // The clock moves on at the start of every minute.
    m_tick.setSingleShot(true);
    connect(&m_tick, &QTimer::timeout, this, &Bar::updateClock);
    updateClock();
}

bool Bar::event(QEvent* event)
{
    // The applications or tray icons came or went.
    if (event->type() == QEvent::LayoutRequest)
        layOut();
    return QWidget::event(event);
}

Bar::~Bar() = default;

void Bar::pickAction(uint id, const QString& action)
{
    const Notification* notification = m_notifications.find(id);
    if (!notification)
        return;
    const QString entry = notification->desktopEntry;
    const QString app = notification->appName;
    // A click on the notification itself is about the program: its window comes forward.
    const bool raise = action.isEmpty() || action == u"default";
    m_windows.requestActivationToken([this, id, action, entry, app, raise](const QString& token) {
        if (!action.isEmpty())
            m_notifications.invoke(id, action, token);
        // Programs that use the token bring the right window forward themselves; the others
        // get their last one raised, a moment later.
        if (raise)
            QTimer::singleShot(300, this, [this, entry, app] { raiseWindowOf(entry, app); });
    });
}

void Bar::raiseWindowOf(const QString& desktopEntry, const QString& appName)
{
    std::vector<quint64> theirs;
    for (const Window* window : m_windows.windows()) {
        if (!isWindowOf(window->appId, desktopEntry, appName))
            continue;
        if (window->activated && !window->minimized)
            return;
        theirs.push_back(window->id);
    }
    for (const quint64 id : m_windows.recent()) {
        if (std::ranges::contains(theirs, id)) {
            m_windows.activate(id);
            return;
        }
    }
}

QMenu* Bar::createQuickSettings()
{
    auto* menu = new QMenu(this);
    auto* panel = new QuickSettings(m_audio, m_brightness, m_network, m_battery, menu);
    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(panel);
    menu->addAction(action);
    connect(menu, &QMenu::aboutToShow, panel, &QuickSettings::refresh);

    // Each after the menu has closed, so a question asked is not under it.
    const auto then = [this, menu](std::function<void()> act) {
        return [menu, act = std::move(act)] {
            menu->close();
            QTimer::singleShot(0, menu, act);
        };
    };
    const QString unsaved = u"Programs that are still open will be closed, and unsaved work in them is lost."_s;
    connect(panel, &QuickSettings::lockRequested, this, then([this] { m_locking->lock(); }));
    connect(panel, &QuickSettings::suspendRequested, this, then([] { power(u"Suspend"_s); }));
    connect(panel, &QuickSettings::restartRequested, this, then([unsaved] {
        if (tde::Dialog::confirm(nullptr, u"Restart"_s, u"Restart the computer?"_s, unsaved, u"Restart"_s))
            power(u"Reboot"_s);
    }));
    connect(panel, &QuickSettings::shutDownRequested, this, then([unsaved] {
        if (tde::Dialog::confirm(nullptr, u"Shut Down"_s, u"Shut down the computer?"_s, unsaved, u"Shut Down"_s))
            power(u"PowerOff"_s);
    }));
    connect(panel, &QuickSettings::logOutRequested, this, then([unsaved] {
        if (tde::Dialog::confirm(nullptr, u"Log Out"_s, u"Log out of this session?"_s, unsaved, u"Log Out"_s))
            logOut();
    }));
    return menu;
}

// The icons of the network, the sound and the battery, side by side, as far as there are.
void Bar::updateStatusIcon()
{
    QStringList names;
    if (m_network.isAvailable())
        names << m_network.iconName();
    if (m_audio.isAvailable())
        names << volumeIconName(m_audio.volume(), m_audio.isMuted());
    if (m_battery.isPresent() && !m_battery.iconName().isEmpty())
        names << m_battery.iconName();
    if (names.isEmpty())
        names << u"system-shutdown-symbolic"_s;

    constexpr int Size = 16;
    constexpr int Gap = 8;
    const qreal ratio = devicePixelRatioF();
    const QSize size(int(names.size()) * Size + (int(names.size()) - 1) * Gap, Size);
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    for (int i = 0; i < names.size(); ++i) {
        const QIcon icon = shell::tintedIcon(names[i], QSize(Size, Size), ratio, tde::theme::colors().text);
        icon.paint(&painter, QRect(i * (Size + Gap), 0, Size, Size));
    }
    painter.end();
    m_system->setIconSize(size);
    m_system->setIcon(QIcon(pixmap));
    layOut();
}

void Bar::updateClock()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale;
    // A dot while notifications wait in the list.
    const bool waiting = std::ranges::any_of(
        m_notifications.notifications(), [](const Notification& notification) { return !notification.banner; });
    m_clock->setText(locale.toString(now, u"ddd d MMM"_s) + u"   "_s + locale.toString(now.time(), QLocale::ShortFormat)
        + (waiting ? u"  •"_s : QString()));
    m_clock->setToolTip(locale.toString(now.date(), QLocale::LongFormat));
    layOut();
    m_tick.start(int(60'000 - now.time().second() * 1000 - now.time().msec()) + 50);
}

void Bar::layOut()
{
    const QRect area = rect().adjusted(Spacing, 0, -Spacing, 0);

    const QSize systemSize(m_system->sizeHint().width(), height());
    m_system->setGeometry(QRect(QPoint(area.right() - systemSize.width() + 1, 0), systemSize));
    const QSize traySize(m_tray->sizeHint().width(), height());
    m_tray->setGeometry(QRect(QPoint(m_system->x() - Spacing - traySize.width(), 0), traySize));

    // The clock in the middle of the screen, unless the applications reach it.
    const QSize clockSize(m_clock->sizeHint().width(), height() - 6);
    int clockX = (width() - clockSize.width()) / 2;
    const int taskbarWidth = m_taskbar->sizeHint().width();
    clockX = std::max(clockX, area.left() + taskbarWidth + Spacing);
    clockX = std::min(clockX, m_tray->x() - Spacing - clockSize.width());
    m_clock->setGeometry(QRect(QPoint(clockX, 3), clockSize));

    m_taskbar->setGeometry(
        area.left(), 0, std::max(0, std::min(taskbarWidth, clockX - Spacing - area.left())), height());
}

void Bar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layOut();
}

void Bar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), tde::theme::colors().header.darker(160));
}

} // namespace hermes
