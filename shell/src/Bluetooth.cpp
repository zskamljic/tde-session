#include "Bluetooth.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>

#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

const QString BlueZ = u"org.bluez"_s;
const QString Adapter = u"org.bluez.Adapter1"_s;
const QString Device = u"org.bluez.Device1"_s;
const QString Battery = u"org.bluez.Battery1"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;
const QString ObjectManager = u"org.freedesktop.DBus.ObjectManager"_s;

using Interfaces = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, Interfaces>;

// What BlueZ says went wrong, put for people.
QString reason(const QDBusError& error)
{
    const QString name = error.name();
    if (name == u"org.bluez.Error.AuthenticationFailed" || name == u"org.bluez.Error.AuthenticationRejected"
        || name == u"org.bluez.Error.AuthenticationCanceled")
        return u"The device did not accept the pairing."_s;
    if (name == u"org.bluez.Error.AuthenticationTimeout" || name == u"org.bluez.Error.ConnectionAttemptFailed")
        return u"The device did not answer. Make sure it is on and nearby."_s;
    if (name == u"org.bluez.Error.Blocked" || name == u"org.bluez.Error.NotReady")
        return u"Bluetooth is turned off or blocked."_s;
    if (name == u"org.bluez.Error.InProgress")
        return u"The device is busy with another request."_s;
    return error.message().isEmpty() ? u"Bluetooth did not do it."_s : error.message();
}

} // namespace

Bluetooth::Bluetooth(QObject* parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<Interfaces>();
    qDBusRegisterMetaType<ManagedObjects>();

    m_refresh.setSingleShot(true);
    m_refresh.setInterval(100);
    connect(&m_refresh, &QTimer::timeout, this, &Bluetooth::refresh);

    auto bus = QDBusConnection::systemBus();
    m_service.setConnection(bus);
    m_service.addWatchedService(BlueZ);
    connect(&m_service, &QDBusServiceWatcher::serviceRegistered, this, &Bluetooth::scheduleRefresh);
    connect(&m_service, &QDBusServiceWatcher::serviceUnregistered, this, &Bluetooth::scheduleRefresh);
    // Anything BlueZ says changed is read again: objects come and go, and their properties change.
    bus.connect(BlueZ, u"/"_s, ObjectManager, u"InterfacesAdded"_s, this, SLOT(scheduleRefresh()));
    bus.connect(BlueZ, u"/"_s, ObjectManager, u"InterfacesRemoved"_s, this, SLOT(scheduleRefresh()));
    bus.connect(BlueZ, {}, Properties, u"PropertiesChanged"_s, this, SLOT(scheduleRefresh()));
    refresh();
}

void Bluetooth::refresh()
{
    auto bus = QDBusConnection::systemBus();
    ManagedObjects objects;
    if (bus.interface()->isServiceRegistered(BlueZ)) {
        const QDBusMessage call = QDBusMessage::createMethodCall(BlueZ, u"/"_s, ObjectManager, u"GetManagedObjects"_s);
        const QDBusReply<ManagedObjects> reply = bus.call(call);
        if (reply.isValid())
            objects = reply.value();
    }

    QString adapter;
    bool powered = false;
    bool discovering = false;
    QString name;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (const auto found = it->find(Adapter); found != it->end()) {
            adapter = it.key().path();
            powered = found->value(u"Powered"_s).toBool();
            discovering = found->value(u"Discovering"_s).toBool();
            name = found->value(u"Alias"_s).toString();
            break;
        }
    }

    std::vector<BluetoothDevice> devices;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto found = it->find(Device);
        if (found == it->end() || found->value(u"Adapter"_s).value<QDBusObjectPath>().path() != adapter)
            continue;
        BluetoothDevice device;
        device.path = it.key().path();
        device.address = found->value(u"Address"_s).toString();
        device.name = found->value(u"Alias"_s).toString();
        device.icon = found->value(u"Icon"_s).toString();
        device.paired = found->value(u"Paired"_s).toBool();
        device.connected = found->value(u"Connected"_s).toBool();
        if (const auto battery = it->find(Battery); battery != it->end())
            device.battery = battery->value(u"Percentage"_s).toInt();
        // Those seen nearby that only have an address are of no use to pick.
        if (!device.paired && !found->contains(u"Name"_s))
            continue;
        devices.push_back(std::move(device));
    }
    std::ranges::sort(devices, [](const BluetoothDevice& a, const BluetoothDevice& b) {
        if (a.paired != b.paired)
            return a.paired;
        if (a.connected != b.connected)
            return a.connected;
        return a.name.localeAwareCompare(b.name) < 0;
    });

    if (adapter == m_adapter && powered == m_powered && discovering == m_discovering && name == m_name
        && devices == m_devices)
        return;
    m_adapter = adapter;
    m_powered = powered;
    m_discovering = discovering;
    m_name = name;
    m_devices = std::move(devices);
    emit changed();
}

std::vector<BluetoothDevice> Bluetooth::paired() const
{
    std::vector<BluetoothDevice> paired;
    std::ranges::copy_if(m_devices, std::back_inserter(paired), &BluetoothDevice::paired);
    return paired;
}

QString Bluetooth::connectedNames() const
{
    QStringList names;
    for (const BluetoothDevice& device : m_devices) {
        if (device.connected)
            names << device.name;
    }
    return names.join(u", "_s);
}

void Bluetooth::call(const QString& path, const QString& interface, const QString& method,
    const QVariantList& arguments, const QString& failure, std::function<void()> then)
{
    QDBusMessage message = QDBusMessage::createMethodCall(BlueZ, path, interface, method);
    message.setArguments(arguments);
    // Pairing waits for the user on both ends.
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, failure, then = std::move(then)] {
        watcher->deleteLater();
        if (watcher->isError()) {
            const QDBusError error = watcher->error();
            // Given up on by the user, which needs no telling.
            if (error.name() != u"org.bluez.Error.AuthenticationCanceled")
                emit failed(failure.isEmpty() ? reason(error) : u"%1 %2"_s.arg(failure, reason(error)));
        } else if (then) {
            then();
        }
        scheduleRefresh();
    });
}

void Bluetooth::setPowered(bool powered)
{
    if (m_adapter.isEmpty())
        return;
    call(m_adapter, Properties, u"Set"_s, {Adapter, u"Powered"_s, QVariant::fromValue(QDBusVariant(powered))},
        powered ? u"Bluetooth could not be turned on."_s : u"Bluetooth could not be turned off."_s);
}

void Bluetooth::setDiscovering(bool discovering)
{
    if (m_adapter.isEmpty() || discovering == m_discovering || (discovering && !m_powered))
        return;
    call(m_adapter, Adapter, discovering ? u"StartDiscovery"_s : u"StopDiscovery"_s, {}, {});
}

void Bluetooth::setDiscoverable(bool discoverable)
{
    if (m_adapter.isEmpty() || !m_powered)
        return;
    call(m_adapter, Properties, u"Set"_s, {Adapter, u"Discoverable"_s, QVariant::fromValue(QDBusVariant(discoverable))},
        {});
}

void Bluetooth::connectDevice(const QString& path)
{
    call(path, Device, u"Connect"_s, {}, u"Could not connect."_s);
}

void Bluetooth::disconnectDevice(const QString& path)
{
    call(path, Device, u"Disconnect"_s, {}, u"Could not disconnect."_s);
}

void Bluetooth::pair(const QString& path)
{
    call(path, Device, u"Pair"_s, {}, u"Could not pair."_s, [this, path] {
        // Trusted, it may connect by itself later, as headphones do when turned on.
        call(path, Properties, u"Set"_s, {Device, u"Trusted"_s, QVariant::fromValue(QDBusVariant(true))}, {});
        connectDevice(path);
    });
}

void Bluetooth::remove(const QString& path)
{
    if (m_adapter.isEmpty())
        return;
    call(m_adapter, Adapter, u"RemoveDevice"_s, {QVariant::fromValue(QDBusObjectPath(path))},
        u"The device could not be forgotten."_s);
}

} // namespace shell
