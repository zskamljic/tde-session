#include "PowerSaving.hpp"

#include <BusProperties.hpp>
#include <LoginSession.hpp>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

const QString UPower = u"org.freedesktop.UPower"_s;
const QString UPowerPath = u"/org/freedesktop/UPower"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;
const QString Login = u"org.freedesktop.login1"_s;
const QString LoginSession = u"org.freedesktop.login1.Session"_s;
// How long the lock screen stays lit without input.
constexpr int LockedBlankSeconds = 60;

} // namespace

PowerSaving::PowerSaving(QObject* parent)
    : QObject(parent)
{
    bind();
    QDBusConnection::systemBus().connect(
        UPower, UPowerPath, Properties, u"PropertiesChanged"_s, this, SLOT(powerSourceChanged()));
    powerSourceChanged();

    // The lock screen tells logind when the session is locked, also when started by the
    // compositor rather than by the bar.
    if (const auto session = shell::loginSession()) {
        m_session = session->path;
        QDBusConnection::systemBus().connect(Login, m_session, Properties, u"PropertiesChanged"_s, this,
            SLOT(sessionChanged(QString, QVariantMap, QStringList)));
        setLocked(
            shell::property(QDBusConnection::systemBus(), Login, m_session, LoginSession, u"LockedHint"_s).toBool());
    }
}

PowerSaving::~PowerSaving()
{
    // The screens are not left dark.
    setScreensOn(true);
}

void PowerSaving::bind()
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland)
        return;
    wl_display* display = wayland->display();
    wl_registry* registry = wl_display_get_registry(display);
    static const wl_registry_listener listener {
        .global =
            [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
                auto* self = static_cast<PowerSaving*>(data);
                if (std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
                    // Version 2 can also tell of input alone, whatever programs ask.
                    self->m_notifierVersion = std::min(version, 2u);
                    self->m_notifier.reset(static_cast<ext_idle_notifier_v1*>(
                        wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, self->m_notifierVersion)));
                } else if (std::strcmp(interface, zwlr_output_power_manager_v1_interface.name) == 0) {
                    self->m_outputPower.reset(static_cast<zwlr_output_power_manager_v1*>(
                        wl_registry_bind(registry, name, &zwlr_output_power_manager_v1_interface, 1)));
                }
            },
        .global_remove = [](void*, wl_registry*, uint32_t) { },
    };
    wl_registry_add_listener(registry, &listener, this);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
}

void PowerSaving::setTimes(int blank, int suspend, int suspendOnBattery)
{
    if (blank == m_blank && suspend == m_suspend && suspendOnBattery == m_suspendOnBattery)
        return;
    m_blank = blank;
    m_suspend = suspend;
    m_suspendOnBattery = suspendOnBattery;
    watch();
}

void PowerSaving::powerSourceChanged()
{
    const bool onBattery
        = shell::property(QDBusConnection::systemBus(), UPower, UPowerPath, UPower, u"OnBattery"_s).toBool();
    if (onBattery == m_onBattery)
        return;
    m_onBattery = onBattery;
    watch();
}

void PowerSaving::sessionChanged(const QString& interface, const QVariantMap& changed, const QStringList&)
{
    if (interface == LoginSession && changed.contains(u"LockedHint"_s))
        setLocked(changed.value(u"LockedHint"_s).toBool());
}

void PowerSaving::setLocked(bool locked)
{
    if (locked == m_locked)
        return;
    m_locked = locked;
    // Unlocked with a finger, the screens may still be dark: no key or movement woke them.
    if (!locked)
        setScreensOn(true);
    watch();
}

// Asks to be told after each while without input, again whenever the times change.
void PowerSaving::watch()
{
    m_blankAfter.reset();
    m_suspendAfter.reset();
    m_lockedBlankAfter.reset();
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!m_notifier || !wayland || !wayland->seat())
        return;
    const auto notification = [&](int minutes) {
        return ext_idle_notifier_v1_get_idle_notification(
            m_notifier.get(), uint32_t(minutes) * 60 * 1000, wayland->seat());
    };
    if (m_blank > 0 && m_outputPower) {
        static const ext_idle_notification_v1_listener listener {
            .idled
            = [](void* data, ext_idle_notification_v1*) { static_cast<PowerSaving*>(data)->setScreensOn(false); },
            .resumed
            = [](void* data, ext_idle_notification_v1*) { static_cast<PowerSaving*>(data)->setScreensOn(true); },
        };
        m_blankAfter.reset(notification(m_blank));
        ext_idle_notification_v1_add_listener(m_blankAfter.get(), &listener, this);
    }
    if (m_locked && m_outputPower) {
        static const ext_idle_notification_v1_listener listener {
            .idled
            = [](void* data, ext_idle_notification_v1*) { static_cast<PowerSaving*>(data)->setScreensOn(false); },
            .resumed
            = [](void* data, ext_idle_notification_v1*) { static_cast<PowerSaving*>(data)->setScreensOn(true); },
        };
        constexpr uint32_t timeout = LockedBlankSeconds * 1000;
        m_lockedBlankAfter.reset(m_notifierVersion >= 2
                ? ext_idle_notifier_v1_get_input_idle_notification(m_notifier.get(), timeout, wayland->seat())
                : ext_idle_notifier_v1_get_idle_notification(m_notifier.get(), timeout, wayland->seat()));
        ext_idle_notification_v1_add_listener(m_lockedBlankAfter.get(), &listener, this);
    }
    if (const int minutes = m_onBattery ? m_suspendOnBattery : m_suspend; minutes > 0) {
        static const ext_idle_notification_v1_listener listener {
            .idled = [](void* data, ext_idle_notification_v1*) { static_cast<PowerSaving*>(data)->suspend(); },
            .resumed = [](void*, ext_idle_notification_v1*) { },
        };
        m_suspendAfter.reset(notification(minutes));
        ext_idle_notification_v1_add_listener(m_suspendAfter.get(), &listener, this);
    }
    wl_display_flush(wayland->display());
}

void PowerSaving::setScreensOn(bool on)
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!m_outputPower || !wayland)
        return;
    if (on) {
        // Turned on by letting go of what turned them off, after saying so.
        for (const auto& screen : m_screens)
            zwlr_output_power_v1_set_mode(screen.get(), ZWLR_OUTPUT_POWER_V1_MODE_ON);
        m_screens.clear();
    } else if (m_screens.empty()) {
        for (QScreen* screen : QGuiApplication::screens()) {
            auto* native = screen->nativeInterface<QNativeInterface::QWaylandScreen>();
            if (!native || !native->output())
                continue;
            auto& power = m_screens.emplace_back(
                zwlr_output_power_manager_v1_get_output_power(m_outputPower.get(), native->output()));
            zwlr_output_power_v1_set_mode(power.get(), ZWLR_OUTPUT_POWER_V1_MODE_OFF);
        }
    }
    wl_display_flush(wayland->display());
}

void PowerSaving::suspend()
{
    // Without asking: it was asked for by setting the time.
    auto message = QDBusMessage::createMethodCall(
        u"org.freedesktop.login1"_s, u"/org/freedesktop/login1"_s, u"org.freedesktop.login1.Manager"_s, u"Suspend"_s);
    message << false;
    QDBusConnection::systemBus().call(message, QDBus::NoBlock);
}

} // namespace hermes
