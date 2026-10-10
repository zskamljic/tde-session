#pragma once

#include <QDBusConnection>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace shell {

// The properties of `interface` of the object at `path` of `service` on `bus`, all at once;
// none when it does not answer within `timeout` ms.
QVariantMap allProperties(const QDBusConnection& bus, const QString& service, const QString& path,
    const QString& interface, int timeout = 2000);

// One of them; null when there is no answer.
QVariant property(const QDBusConnection& bus, const QString& service, const QString& path, const QString& interface,
    const QString& name, int timeout = 2000);

} // namespace shell
