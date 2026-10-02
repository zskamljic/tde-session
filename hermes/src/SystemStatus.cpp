#include "SystemStatus.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

const QString Properties = u"org.freedesktop.DBus.Properties"_s;

QVariantMap allProperties(const QString& service, const QString& path, const QString& interface)
{
    auto message = QDBusMessage::createMethodCall(service, path, Properties, u"GetAll"_s);
    message << interface;
    const QDBusReply<QVariantMap> reply = QDBusConnection::systemBus().call(message, QDBus::Block, 2000);
    return reply.isValid() ? reply.value() : QVariantMap();
}

QString duration(qint64 seconds)
{
    const qint64 minutes = (seconds + 30) / 60;
    return minutes >= 60 ? u"%1:%2"_s.arg(minutes / 60).arg(minutes % 60, 2, 10, u'0') : u"%1 min"_s.arg(minutes);
}

int readNumber(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    bool ok = false;
    const int value = file.readAll().trimmed().toInt(&ok);
    return ok ? value : -1;
}

} // namespace

// Battery ---------------------------------------------------------------------------------

namespace {
const QString UPower = u"org.freedesktop.UPower"_s;
const QString DisplayDevice = u"/org/freedesktop/UPower/devices/DisplayDevice"_s;
const QString DeviceInterface = u"org.freedesktop.UPower.Device"_s;
} // namespace

Battery::Battery(QObject* parent)
    : QObject(parent)
{
    QDBusConnection::systemBus().connect(
        UPower, DisplayDevice, Properties, u"PropertiesChanged"_s, this, SLOT(refresh()));
    refresh();
}

void Battery::refresh()
{
    const QVariantMap properties = allProperties(UPower, DisplayDevice, DeviceInterface);
    // The display device stands for all batteries together; type 2 is a battery.
    const bool present = properties.value(u"IsPresent"_s).toBool() && properties.value(u"Type"_s).toUInt() == 2;
    const int percentage = int(std::lround(properties.value(u"Percentage"_s).toDouble()));
    const uint state = properties.value(u"State"_s).toUInt();
    const bool charging = state == 1 || state == 5;
    QString time;
    if (state == 4)
        time = u"Fully charged"_s;
    else if (state == 1 && properties.value(u"TimeToFull"_s).toLongLong() > 0)
        time = u"%1 until full"_s.arg(duration(properties.value(u"TimeToFull"_s).toLongLong()));
    else if (state == 2 && properties.value(u"TimeToEmpty"_s).toLongLong() > 0)
        time = u"%1 left"_s.arg(duration(properties.value(u"TimeToEmpty"_s).toLongLong()));
    const QString icon = properties.value(u"IconName"_s).toString();

    if (present == m_present && percentage == m_percentage && charging == m_charging && time == m_time
        && icon == m_icon)
        return;
    m_present = present;
    m_percentage = percentage;
    m_charging = charging;
    m_time = time;
    m_icon = icon;
    emit changed();
}

// Network ---------------------------------------------------------------------------------

namespace {
const QString NetworkManager = u"org.freedesktop.NetworkManager"_s;
const QString NetworkManagerPath = u"/org/freedesktop/NetworkManager"_s;
} // namespace

Network::Network(QObject* parent)
    : QObject(parent)
{
    auto bus = QDBusConnection::systemBus();
    m_service.setConnection(bus);
    m_service.addWatchedService(NetworkManager);
    connect(&m_service, &QDBusServiceWatcher::serviceRegistered, this, &Network::refresh);
    connect(&m_service, &QDBusServiceWatcher::serviceUnregistered, this, &Network::refresh);
    bus.connect(NetworkManager, NetworkManagerPath, Properties, u"PropertiesChanged"_s, this, SLOT(refresh()));
    refresh();
}

void Network::refresh()
{
    const bool available = QDBusConnection::systemBus().interface()->isServiceRegistered(NetworkManager);
    const QVariantMap manager
        = available ? allProperties(NetworkManager, NetworkManagerPath, NetworkManager) : QVariantMap();

    // NM_STATE: 70 connected, 60 and 50 partly, 40 connecting, 20 and below not.
    const uint state = manager.value(u"State"_s).toUInt();
    const bool connected = state >= 50;
    const bool wifi = manager.value(u"WirelessEnabled"_s).toBool();
    const bool wifiHardware = manager.value(u"WirelessHardwareEnabled"_s).toBool();

    QString description = u"Offline"_s;
    QString icon = u"network-offline-symbolic"_s;
    if (state == 40) {
        description = u"Connecting…"_s;
        icon = u"network-idle-symbolic"_s;
    } else if (connected) {
        const QString primary = manager.value(u"PrimaryConnection"_s).value<QDBusObjectPath>().path();
        const QVariantMap active
            = allProperties(NetworkManager, primary, u"org.freedesktop.NetworkManager.Connection.Active"_s);
        const QString type = active.value(u"Type"_s).toString();
        description = active.value(u"Id"_s).toString();
        icon = type == u"802-11-wireless"            ? u"network-wireless-signal-good-symbolic"_s
            : type == u"vpn" || type == u"wireguard" ? u"network-vpn-symbolic"_s
                                                     : u"network-wired-symbolic"_s;
        if (description.isEmpty())
            description = u"Connected"_s;
    }

    if (available == m_available && connected == m_connected && wifi == m_wifi && wifiHardware == m_wifiHardware
        && description == m_description && icon == m_icon)
        return;
    m_available = available;
    m_connected = connected;
    m_wifi = wifi;
    m_wifiHardware = wifiHardware;
    m_description = description;
    m_icon = icon;
    emit changed();
}

void Network::setWifiEnabled(bool enabled)
{
    auto message = QDBusMessage::createMethodCall(NetworkManager, NetworkManagerPath, Properties, u"Set"_s);
    message << NetworkManager << u"WirelessEnabled"_s << QVariant::fromValue(QDBusVariant(enabled));
    QDBusConnection::systemBus().call(message, QDBus::NoBlock);
}

// Brightness ------------------------------------------------------------------------------

Brightness::Brightness(QObject* parent)
    : QObject(parent)
{
    // The firmware's control is the one meant for people, then the platform's, then the
    // graphics driver's.
    const QDir backlights(u"/sys/class/backlight"_s);
    const QStringList devices = backlights.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const char* wanted : {"firmware", "platform", "raw"}) {
        const auto it = std::ranges::find_if(devices, [&](const QString& device) {
            QFile type(backlights.filePath(device + u"/type"_s));
            return type.open(QIODevice::ReadOnly) && type.readAll().trimmed() == wanted;
        });
        if (it != devices.end()) {
            m_device = *it;
            break;
        }
    }
    if (m_device.isEmpty())
        return;
    m_maximum = readNumber(backlights.filePath(m_device + u"/max_brightness"_s));
    if (m_maximum <= 0)
        m_device.clear();
    refresh();
}

double Brightness::value() const
{
    return m_maximum > 0 ? double(m_current) / m_maximum : 0;
}

void Brightness::refresh()
{
    if (m_device.isEmpty())
        return;
    const int current = readNumber(u"/sys/class/backlight/%1/brightness"_s.arg(m_device));
    if (current >= 0 && current != m_current) {
        m_current = current;
        emit changed();
    }
}

void Brightness::setValue(double value)
{
    if (m_device.isEmpty())
        return;
    // Never quite dark: a screen at 0 can look switched off.
    const int minimum = std::max(1, m_maximum / 100);
    const int target = std::clamp(int(std::lround(value * m_maximum)), minimum, m_maximum);
    auto message = QDBusMessage::createMethodCall(u"org.freedesktop.login1"_s,
        u"/org/freedesktop/login1/session/auto"_s, u"org.freedesktop.login1.Session"_s, u"SetBrightness"_s);
    message << u"backlight"_s << m_device << uint(target);
    QDBusConnection::systemBus().call(message, QDBus::NoBlock);
    m_current = target;
    emit changed();
}

} // namespace hermes
