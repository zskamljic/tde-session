#include "Locking.hpp"

#include <LoginSession.hpp>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QGuiApplication>
#include <QProcess>

#include <cstring>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

const QString Login = u"org.freedesktop.login1"_s;
const QString LoginPath = u"/org/freedesktop/login1"_s;
const QString Manager = u"org.freedesktop.login1.Manager"_s;

} // namespace

Locking::Locking(QObject* parent)
    : QObject(parent)
{
    bindIdle();

    auto bus = QDBusConnection::systemBus();
    bus.connect(Login, LoginPath, Manager, u"PrepareForSleep"_s, this, SLOT(prepareForSleep(bool)));
    takeSleepInhibitor();

    // The session this bar belongs to, which `loginctl lock-session` asks to lock.
    if (const auto session = shell::loginSession())
        bus.connect(Login, session->path, u"org.freedesktop.login1.Session"_s, u"Lock"_s, this, SLOT(lockRequested()));
    else
        qWarning("tde-hermes: no login session to lock on request");
}

Locking::~Locking() = default;

void Locking::bindIdle()
{
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland)
        return;
    wl_display* display = wayland->display();
    wl_registry* registry = wl_display_get_registry(display);
    static const wl_registry_listener listener {
        .global =
            [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
                if (std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0)
                    static_cast<Locking*>(data)->m_notifier.reset(static_cast<ext_idle_notifier_v1*>(
                        wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1)));
            },
        .global_remove = [](void*, wl_registry*, uint32_t) { },
    };
    wl_registry_add_listener(registry, &listener, this);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    if (!m_notifier)
        qWarning("tde-hermes: the compositor does not say when the user is away; no locking then");
}

void Locking::setIdleMinutes(int minutes)
{
    if (minutes == m_idleMinutes && (m_idle || minutes == 0))
        return;
    m_idleMinutes = minutes;
    m_idle.reset();
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (minutes <= 0 || !m_notifier || !wayland || !wayland->seat())
        return;

    // Version 1 notifications wait while programs ask the screen to stay on.
    m_idle.reset(
        ext_idle_notifier_v1_get_idle_notification(m_notifier.get(), uint32_t(minutes) * 60 * 1000, wayland->seat()));
    static const ext_idle_notification_v1_listener idleListener {
        .idled = [](void* data, ext_idle_notification_v1*) { static_cast<Locking*>(data)->lock(); },
        .resumed = [](void*, ext_idle_notification_v1*) { },
    };
    ext_idle_notification_v1_add_listener(m_idle.get(), &idleListener, this);
    wl_display_flush(wayland->display());
}

void Locking::lock(std::function<void()> whenLocked)
{
    if (whenLocked) {
        if (m_locked)
            whenLocked();
        else
            m_waiting.push_back(std::move(whenLocked));
    }
    if (m_locker)
        return;

    m_locker = std::make_unique<QProcess>();
    m_locker->setProgram(u"tde-cerberus"_s);
    if (m_seconds)
        m_locker->setArguments({u"--seconds"_s});
    m_locker->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_locker.get(), &QProcess::readyReadStandardOutput, this, [this] {
        if (m_locker->readAllStandardOutput().contains("locked")) {
            m_locked = true;
            settled();
        }
    });
    connect(m_locker.get(), &QProcess::finished, this, [this] {
        // Unlocked, or refused as another lock screen holds the session: either way, nothing
        // is waiting for this one any more. It goes once its signal is over.
        m_locker.release()->deleteLater();
        m_locked = false;
        settled();
    });
    connect(m_locker.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning("tde-hermes: could not start tde-cerberus to lock the screen");
        m_locker.release()->deleteLater();
        settled();
    });
    m_locker->start();
}

void Locking::settled()
{
    for (const auto& waiting : std::exchange(m_waiting, {}))
        waiting();
}

void Locking::lockRequested()
{
    lock();
}

void Locking::takeSleepInhibitor()
{
    // Sleep waits a moment for the screen to be locked, so it is when the computer wakes.
    auto inhibit = QDBusMessage::createMethodCall(Login, LoginPath, Manager, u"Inhibit"_s);
    inhibit << u"sleep"_s << u"TDE"_s << u"Locking the screen before sleeping"_s << u"delay"_s;
    const QDBusReply<QDBusUnixFileDescriptor> reply = QDBusConnection::systemBus().call(inhibit);
    if (reply.isValid())
        m_sleepInhibitor = reply.value();
    else
        qWarning("tde-hermes: could not delay sleep to lock first: %s", qPrintable(reply.error().message()));
}

void Locking::prepareForSleep(bool sleeping)
{
    if (!sleeping) {
        // Awake again: ready for the next time.
        takeSleepInhibitor();
        return;
    }
    lock([this] { m_sleepInhibitor = QDBusUnixFileDescriptor(); });
}

} // namespace hermes
