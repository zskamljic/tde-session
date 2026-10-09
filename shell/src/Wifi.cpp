#include "Wifi.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>

#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

const QString NetworkManager = u"org.freedesktop.NetworkManager"_s;
const QString ManagerPath = u"/org/freedesktop/NetworkManager"_s;
const QString SettingsPath = u"/org/freedesktop/NetworkManager/Settings"_s;
const QString Device = u"org.freedesktop.NetworkManager.Device"_s;
const QString Wireless = u"org.freedesktop.NetworkManager.Device.Wireless"_s;
const QString AccessPoint = u"org.freedesktop.NetworkManager.AccessPoint"_s;
const QString Settings = u"org.freedesktop.NetworkManager.Settings"_s;
const QString Connection = u"org.freedesktop.NetworkManager.Settings.Connection"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;

// NM_DEVICE_TYPE and NM_DEVICE_STATE values used here.
constexpr uint TypeEthernet = 1;
constexpr uint TypeWifi = 2;
constexpr uint StateUnmanaged = 10;
constexpr uint StateUnavailable = 20;
constexpr uint StatePrepare = 40;
constexpr uint StateActivated = 100;
constexpr uint StateFailed = 120;

using ConnectionSettings = QMap<QString, QVariantMap>;

QVariantMap properties(const QString& path, const QString& interface)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NetworkManager, path, Properties, u"GetAll"_s);
    call << interface;
    const QDBusReply<QVariantMap> reply = QDBusConnection::systemBus().call(call);
    return reply.isValid() ? reply.value() : QVariantMap();
}

QList<QDBusObjectPath> paths(const QVariant& value)
{
    return qdbus_cast<QList<QDBusObjectPath>>(value);
}

// What went wrong with connecting, from NM_DEVICE_STATE_REASON, for people.
QString reasonText(uint reason)
{
    switch (reason) {
    case 7: // no secrets
    case 8: // supplicant disconnected, as a wrong password does
    case 9: // supplicant configuration failed
        return u"The password was not accepted."_s;
    case 11: // supplicant timed out
        return u"The network did not answer."_s;
    case 5: // IP configuration unavailable
    case 17: // DHCP failed
        return u"The network did not give this computer an address."_s;
    default:
        return u"Could not connect to the network."_s;
    }
}

} // namespace

QString wifiIconName(int strength)
{
    if (strength < 0)
        return u"network-wireless-offline-symbolic"_s;
    if (strength < 25)
        return u"network-wireless-signal-weak-symbolic"_s;
    if (strength < 50)
        return u"network-wireless-signal-ok-symbolic"_s;
    if (strength < 75)
        return u"network-wireless-signal-good-symbolic"_s;
    return u"network-wireless-signal-excellent-symbolic"_s;
}

Wifi::Wifi(QObject* parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<ConnectionSettings>();
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(200);
    connect(&m_refresh, &QTimer::timeout, this, &Wifi::refresh);

    auto bus = QDBusConnection::systemBus();
    m_service.setConnection(bus);
    m_service.addWatchedService(NetworkManager);
    connect(&m_service, &QDBusServiceWatcher::serviceRegistered, this, &Wifi::scheduleRefresh);
    connect(&m_service, &QDBusServiceWatcher::serviceUnregistered, this, &Wifi::scheduleRefresh);
    // Access points, devices and connections change all the time; it is all read again.
    bus.connect(NetworkManager, {}, Properties, u"PropertiesChanged"_s, this, SLOT(scheduleRefresh()));
    bus.connect(NetworkManager, {}, Wireless, u"AccessPointAdded"_s, this, SLOT(scheduleRefresh()));
    bus.connect(NetworkManager, {}, Wireless, u"AccessPointRemoved"_s, this, SLOT(scheduleRefresh()));
    bus.connect(NetworkManager, SettingsPath, Settings, u"NewConnection"_s, this, SLOT(scheduleRefresh()));
    bus.connect(NetworkManager, SettingsPath, Settings, u"ConnectionRemoved"_s, this, SLOT(scheduleRefresh()));
    bus.connect(NetworkManager, {}, Device, u"StateChanged"_s, this, SLOT(deviceStateChanged(uint, uint, uint)));
    refresh();
}

void Wifi::refresh()
{
    auto bus = QDBusConnection::systemBus();
    QString device;
    bool enabled = false;
    std::vector<WifiNetwork> networks;
    std::vector<WiredDevice> wired;
    if (bus.interface()->isServiceRegistered(NetworkManager)) {
        const QVariantMap manager = properties(ManagerPath, NetworkManager);
        enabled = manager.value(u"WirelessEnabled"_s).toBool();
        for (const QDBusObjectPath& path : paths(manager.value(u"Devices"_s))) {
            const QVariantMap info = properties(path.path(), Device);
            const uint type = info.value(u"DeviceType"_s).toUInt();
            const uint state = info.value(u"State"_s).toUInt();
            // Devices NetworkManager leaves alone are not its to tell of.
            if (state <= StateUnmanaged)
                continue;
            if (type == TypeEthernet) {
                wired.push_back({path.path(), info.value(u"Interface"_s).toString(), state == StateActivated,
                    state > StateUnavailable});
            } else if (type == TypeWifi && device.isEmpty()) {
                device = path.path();
            }
        }
    }

    if (!device.isEmpty()) {
        // The networks in range, each as strong as its strongest access point.
        const QVariantMap wireless = properties(device, Wireless);
        const QString activePoint = wireless.value(u"ActiveAccessPoint"_s).value<QDBusObjectPath>().path();
        const uint state = properties(device, Device).value(u"State"_s).toUInt();
        const bool busy = state >= StatePrepare && state <= StateActivated;
        std::map<QString, WifiNetwork> found;
        for (const QDBusObjectPath& point : paths(wireless.value(u"AccessPoints"_s))) {
            const QVariantMap info = properties(point.path(), AccessPoint);
            const QString ssid = QString::fromUtf8(info.value(u"Ssid"_s).toByteArray());
            if (ssid.isEmpty())
                continue; // hidden ones are not offered
            const int strength = info.value(u"Strength"_s).toInt();
            const bool secured = (info.value(u"Flags"_s).toUInt() & 1) || info.value(u"WpaFlags"_s).toUInt()
                || info.value(u"RsnFlags"_s).toUInt();
            WifiNetwork& network = found[ssid];
            network.ssid = ssid;
            network.secured = network.secured || secured;
            network.active = network.active || (busy && point.path() == activePoint);
            if (strength > network.strength) {
                network.strength = strength;
                network.accessPoint = point.path();
            }
        }
        // The networks kept, in range or not.
        QDBusMessage list
            = QDBusMessage::createMethodCall(NetworkManager, SettingsPath, Settings, u"ListConnections"_s);
        const QDBusReply<QList<QDBusObjectPath>> kept = bus.call(list);
        for (const QDBusObjectPath& path : kept.isValid() ? kept.value() : QList<QDBusObjectPath>()) {
            const QDBusMessage call
                = QDBusMessage::createMethodCall(NetworkManager, path.path(), Connection, u"GetSettings"_s);
            const QDBusReply<ConnectionSettings> settings = bus.call(call);
            if (!settings.isValid())
                continue;
            const QVariantMap wifi = settings.value().value(u"802-11-wireless"_s);
            const QString ssid = QString::fromUtf8(wifi.value(u"ssid"_s).toByteArray());
            if (ssid.isEmpty())
                continue;
            WifiNetwork& network = found[ssid];
            network.ssid = ssid;
            network.connection = path.path();
            if (settings.value().contains(u"802-11-wireless-security"_s))
                network.secured = true;
        }
        for (auto& [ssid, network] : found) {
            network.passwordRejected = m_rejected.contains(ssid);
            networks.push_back(std::move(network));
        }
        std::ranges::sort(networks, [](const WifiNetwork& a, const WifiNetwork& b) {
            if ((a.strength >= 0) != (b.strength >= 0))
                return a.strength >= 0;
            if (a.active != b.active)
                return a.active;
            if (a.strength != b.strength)
                return a.strength > b.strength;
            return a.ssid.localeAwareCompare(b.ssid) < 0;
        });
    }

    if (device == m_device && enabled == m_enabled && networks == m_networks && wired == m_wired)
        return;
    m_device = device;
    m_enabled = enabled;
    m_networks = std::move(networks);
    m_wired = std::move(wired);
    emit changed();
}

void Wifi::deviceStateChanged(uint state, uint, uint reason)
{
    // Only the Wi-Fi device's failures are told: a cable unplugged is no failure to report.
    if (state == StateFailed && message().path() == m_device) {
        // A password not accepted is asked for again next time.
        if (reason >= 7 && reason <= 9 && !m_connecting.isEmpty() && !m_rejected.contains(m_connecting))
            m_rejected << m_connecting;
        emit failed(reasonText(reason));
    } else if (state == StateActivated && message().path() == m_device) {
        m_rejected.removeAll(m_connecting);
    }
    scheduleRefresh();
}

void Wifi::call(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments,
    const QString& failure)
{
    QDBusMessage message = QDBusMessage::createMethodCall(NetworkManager, path, interface, method);
    message.setArguments(arguments);
    message.setInteractiveAuthorizationAllowed(true);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, failure] {
        watcher->deleteLater();
        if (watcher->isError() && !failure.isEmpty())
            emit failed(failure);
        scheduleRefresh();
    });
}

void Wifi::setEnabled(bool enabled)
{
    call(ManagerPath, Properties, u"Set"_s,
        {NetworkManager, u"WirelessEnabled"_s, QVariant::fromValue(QDBusVariant(enabled))},
        enabled ? u"Wi-Fi could not be turned on."_s : u"Wi-Fi could not be turned off."_s);
}

void Wifi::scan()
{
    if (!m_device.isEmpty())
        call(m_device, Wireless, u"RequestScan"_s, {QVariantMap()}, {});
}

void Wifi::connectTo(const QString& ssid, const QString& password)
{
    const auto network = std::ranges::find(m_networks, ssid, &WifiNetwork::ssid);
    if (network == m_networks.end() || m_device.isEmpty())
        return;
    const auto device = QVariant::fromValue(QDBusObjectPath(m_device));
    m_connecting = ssid;
    if (!password.isEmpty())
        m_rejected.removeAll(ssid);
    if (!network->connection.isEmpty() && password.isEmpty()) {
        call(ManagerPath, NetworkManager, u"ActivateConnection"_s,
            {QVariant::fromValue(QDBusObjectPath(network->connection)), device,
                QVariant::fromValue(QDBusObjectPath(u"/"_s))},
            u"Could not connect to %1."_s.arg(ssid));
        return;
    }
    // A new connection: NetworkManager fills in the rest from the access point.
    ConnectionSettings settings;
    if (network->secured) {
        settings[u"802-11-wireless-security"_s] = {{u"key-mgmt"_s, u"wpa-psk"_s}, {u"psk"_s, password}};
    }
    // One kept before is replaced by the new one, with the password typed now: gone first, so the
    // new one takes its name.
    if (!network->connection.isEmpty()) {
        QDBusMessage remove
            = QDBusMessage::createMethodCall(NetworkManager, network->connection, Connection, u"Delete"_s);
        remove.setInteractiveAuthorizationAllowed(true);
        QDBusConnection::systemBus().call(remove, QDBus::Block, 120000);
    }
    call(ManagerPath, NetworkManager, u"AddAndActivateConnection"_s,
        {QVariant::fromValue(settings), device, QVariant::fromValue(QDBusObjectPath(network->accessPoint))},
        u"Could not connect to %1."_s.arg(ssid));
}

void Wifi::disconnect()
{
    if (!m_device.isEmpty())
        call(m_device, Device, u"Disconnect"_s, {}, {});
}

std::optional<QString> Wifi::password(const QString& ssid) const
{
    const auto network = std::ranges::find(m_networks, ssid, &WifiNetwork::ssid);
    if (network == m_networks.end() || network->connection.isEmpty())
        return std::nullopt;
    QDBusMessage call = QDBusMessage::createMethodCall(NetworkManager, network->connection, Connection, u"GetSecrets"_s);
    call << u"802-11-wireless-security"_s;
    call.setInteractiveAuthorizationAllowed(true);
    // Long enough for a password to be typed, when polkit asks for one.
    const QDBusReply<ConnectionSettings> secrets = QDBusConnection::systemBus().call(call, QDBus::Block, 120000);
    if (!secrets.isValid())
        return std::nullopt;
    const QVariantMap security = secrets.value().value(u"802-11-wireless-security"_s);
    for (const char* key : {"psk", "wep-key0", "leap-password"}) {
        if (const QString value = security.value(QString::fromLatin1(key)).toString(); !value.isEmpty())
            return value;
    }
    return std::nullopt;
}

void Wifi::forget(const QString& ssid)
{
    const auto network = std::ranges::find(m_networks, ssid, &WifiNetwork::ssid);
    if (network != m_networks.end() && !network->connection.isEmpty())
        call(network->connection, Connection, u"Delete"_s, {}, u"%1 could not be forgotten."_s.arg(ssid));
}

} // namespace shell
