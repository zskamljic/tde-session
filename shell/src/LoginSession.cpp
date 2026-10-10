#include "LoginSession.hpp"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

const QString Login = u"org.freedesktop.login1"_s;
const QString Manager = u"org.freedesktop.login1.Manager"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;

QVariant property(const QString& path, const QString& interface, const QString& name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Login, path, Properties, u"Get"_s);
    call << interface << name;
    const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(call);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

} // namespace

std::optional<LoginSession> loginSession()
{
    const auto bus = QDBusConnection::systemBus();
    QDBusMessage byPid
        = QDBusMessage::createMethodCall(Login, u"/org/freedesktop/login1"_s, Manager, u"GetSessionByPID"_s);
    byPid << uint(QCoreApplication::applicationPid());
    if (const QDBusReply<QDBusObjectPath> session = bus.call(byPid); session.isValid()) {
        const QString path = session.value().path();
        return LoginSession {property(path, u"org.freedesktop.login1.Session"_s, u"Id"_s).toString(), path};
    }

    // Outside any session: the user's graphical one, as logind names it with (so).
    QDBusMessage byUser = QDBusMessage::createMethodCall(Login, u"/org/freedesktop/login1"_s, Manager, u"GetUser"_s);
    byUser << uint(getuid());
    const QDBusReply<QDBusObjectPath> user = bus.call(byUser);
    if (!user.isValid())
        return std::nullopt;
    const QVariant display = property(user.value().path(), u"org.freedesktop.login1.User"_s, u"Display"_s);
    if (!display.canConvert<QDBusArgument>())
        return std::nullopt;
    LoginSession found;
    QDBusObjectPath path;
    const QDBusArgument argument = display.value<QDBusArgument>();
    argument.beginStructure();
    argument >> found.id >> path;
    argument.endStructure();
    found.path = path.path();
    if (found.id.isEmpty() || found.path == u"/")
        return std::nullopt;
    return found;
}

} // namespace shell
