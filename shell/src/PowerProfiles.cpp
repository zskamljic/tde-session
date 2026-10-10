#include "PowerProfiles.hpp"

#include "BusProperties.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

const QString Properties = u"org.freedesktop.DBus.Properties"_s;
const QString Order[] = {u"performance"_s, u"balanced"_s, u"power-saver"_s};

} // namespace

PowerProfiles::PowerProfiles(QObject* parent)
    : QObject(parent)
{
    // By the name it has now, or the one it had before.
    auto bus = QDBusConnection::systemBus();
    for (const auto& [service, path] : {
             std::pair {u"org.freedesktop.UPower.PowerProfiles"_s, u"/org/freedesktop/UPower/PowerProfiles"_s},
             std::pair {u"net.hadess.PowerProfiles"_s, u"/net/hadess/PowerProfiles"_s},
         }) {
        if (bus.interface()->isServiceRegistered(service)
            || bus.interface()->activatableServiceNames().value().contains(service)) {
            m_service = service;
            m_path = path;
            m_interface = service;
            break;
        }
    }
    if (m_service.isEmpty())
        return;
    bus.connect(m_service, m_path, Properties, u"PropertiesChanged"_s, this, SLOT(refresh()));
    refresh();
}

void PowerProfiles::refresh()
{
    const QVariantMap properties = allProperties(QDBusConnection::systemBus(), m_service, m_path, m_interface);
    if (properties.isEmpty())
        return;
    const QString active = properties.value(u"ActiveProfile"_s).toString();
    const auto profiles = qdbus_cast<QList<QVariantMap>>(properties.value(u"Profiles"_s));
    QStringList offered;
    for (const QString& name : Order) {
        if (std::ranges::any_of(profiles, [&](const QVariantMap& p) { return p.value(u"Profile"_s) == name; }))
            offered << name;
    }
    if (active == m_active && offered == m_offered)
        return;
    m_active = active;
    m_offered = offered;
    emit changed();
}

void PowerProfiles::setActive(const QString& profile)
{
    if (!isAvailable())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, Properties, u"Set"_s);
    call << m_interface << u"ActiveProfile"_s << QVariant::fromValue(QDBusVariant(profile));
    call.setInteractiveAuthorizationAllowed(true);
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
}

QString powerProfileName(const QString& profile)
{
    if (profile == u"performance")
        return u"Performance"_s;
    if (profile == u"power-saver")
        return u"Power Saver"_s;
    return u"Balanced"_s;
}

QString powerProfileIcon(const QString& profile)
{
    return u"power-profile-%1-symbolic"_s.arg(profile.isEmpty() ? u"balanced"_s : profile);
}

} // namespace shell
