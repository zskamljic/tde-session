#include "BusProperties.hpp"

#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

const QString Properties = u"org.freedesktop.DBus.Properties"_s;

} // namespace

QVariantMap allProperties(
    const QDBusConnection& bus, const QString& service, const QString& path, const QString& interface, int timeout)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, Properties, u"GetAll"_s);
    call << interface;
    const QDBusReply<QVariantMap> reply = bus.call(call, QDBus::Block, timeout);
    return reply.isValid() ? reply.value() : QVariantMap();
}

QVariant property(const QDBusConnection& bus, const QString& service, const QString& path, const QString& interface,
    const QString& name, int timeout)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, Properties, u"Get"_s);
    call << interface << name;
    const QDBusReply<QDBusVariant> reply = bus.call(call, QDBus::Block, timeout);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

} // namespace shell
