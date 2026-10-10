#include "BluetoothPage.hpp"

#include <Layouts.hpp>
#include <tde/Dialog.hpp>
#include <tde/Toast.hpp>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

const QString AgentPath = u"/io/github/zskamljic/Daedalus/Agent"_s;

QDBusMessage agentManager(const QString& method)
{
    return QDBusMessage::createMethodCall(u"org.bluez"_s, u"/org/bluez"_s, u"org.bluez.AgentManager1"_s, method);
}

QString passkeyText(uint passkey)
{
    return u"%1"_s.arg(passkey, 6, 10, QChar(u'0'));
}

} // namespace

// PairingAgent ----------------------------------------------------------------------------

PairingAgent::PairingAgent(const shell::Bluetooth& bluetooth, QWidget* window)
    : m_bluetooth(bluetooth)
    , m_window(window)
    , m_toast(new tde::Toast(window))
{
    auto bus = QDBusConnection::systemBus();
    if (!bus.registerObject(AgentPath, this, QDBusConnection::ExportAllSlots))
        return;
    // Able to show codes and to have them typed, so every kind of device can pair.
    QDBusMessage registration = agentManager(u"RegisterAgent"_s);
    registration << QVariant::fromValue(QDBusObjectPath(AgentPath)) << u"KeyboardDisplay"_s;
    if (bus.call(registration).type() == QDBusMessage::ErrorMessage)
        return;
    m_registered = true;
    QDBusMessage preferred = agentManager(u"RequestDefaultAgent"_s);
    preferred << QVariant::fromValue(QDBusObjectPath(AgentPath));
    bus.call(preferred, QDBus::NoBlock);
}

PairingAgent::~PairingAgent()
{
    auto bus = QDBusConnection::systemBus();
    if (m_registered) {
        QDBusMessage message = agentManager(u"UnregisterAgent"_s);
        message << QVariant::fromValue(QDBusObjectPath(AgentPath));
        bus.call(message, QDBus::NoBlock);
    }
    bus.unregisterObject(AgentPath);
}

QString PairingAgent::nameOf(const QDBusObjectPath& device) const
{
    for (const shell::BluetoothDevice& known : m_bluetooth.devices()) {
        if (known.path == device.path())
            return known.name;
    }
    return u"The device"_s;
}

void PairingAgent::reject()
{
    sendErrorReply(u"org.bluez.Error.Rejected"_s, u"Declined by the user"_s);
}

QString PairingAgent::RequestPinCode(const QDBusObjectPath& device)
{
    const auto pin = tde::Dialog::getText(m_window, u"Pair"_s,
        u"Type the PIN for %1, as its manual or screen gives it:"_s.arg(nameOf(device)), {}, u"Pair"_s);
    if (!pin || pin->isEmpty()) {
        reject();
        return {};
    }
    return *pin;
}

void PairingAgent::DisplayPinCode(const QDBusObjectPath& device, const QString& pinCode)
{
    m_toast->showMessage(u"Type %1 on %2, then Enter"_s.arg(pinCode, nameOf(device)), 30000);
}

uint PairingAgent::RequestPasskey(const QDBusObjectPath& device)
{
    const auto passkey
        = tde::Dialog::getText(m_window, u"Pair"_s, u"Type the code %1 shows:"_s.arg(nameOf(device)), {}, u"Pair"_s);
    bool number = false;
    const uint value = passkey ? passkey->trimmed().toUInt(&number) : 0;
    if (!number) {
        reject();
        return 0;
    }
    return value;
}

void PairingAgent::DisplayPasskey(const QDBusObjectPath& device, uint passkey, ushort)
{
    m_toast->showMessage(u"Type %1 on %2, then Enter"_s.arg(passkeyText(passkey), nameOf(device)), 30000);
}

void PairingAgent::RequestConfirmation(const QDBusObjectPath& device, uint passkey)
{
    const QString name = nameOf(device);
    if (!tde::Dialog::confirm(m_window, u"Pair"_s, u"Does %1 show %2?"_s.arg(name, passkeyText(passkey)),
            u"Pair only when the codes match."_s, u"Pair"_s, false))
        reject();
}

void PairingAgent::RequestAuthorization(const QDBusObjectPath& device)
{
    if (!tde::Dialog::confirm(m_window, u"Pair"_s, u"Pair with %1?"_s.arg(nameOf(device)), {}, u"Pair"_s, false))
        reject();
}

void PairingAgent::AuthorizeService(const QDBusObjectPath&, const QString&)
{
    // The devices that ask are paired already, which the user agreed to.
}

// BluetoothPage ---------------------------------------------------------------------------

BluetoothPage::BluetoothPage(QWidget* parent)
    : Page(parent)
    , m_toast(new tde::Toast(this))
{
    m_content = new QWidget(this);
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);
    addWidget(m_content);
    connect(&m_bluetooth, &shell::Bluetooth::changed, this, &BluetoothPage::sync);
    connect(&m_bluetooth, &shell::Bluetooth::failed, this, [this](const QString& message) {
        m_toast->showMessage(message, 6000);
        // A pairing that failed lets its button be pressed again.
        if (!m_pairing.isEmpty()) {
            m_pairing.clear();
            m_shown.clear();
            sync();
        }
    });
    sync();
}

BluetoothPage::~BluetoothPage()
{
    m_bluetooth.setDiscovering(false);
    m_bluetooth.setDiscoverable(false);
}

void BluetoothPage::showEvent(QShowEvent* event)
{
    Page::showEvent(event);
    if (!m_agent)
        m_agent = std::make_unique<PairingAgent>(m_bluetooth, window());
    // Looking for devices nearby, and found by them, while the page shows, as GNOME does.
    m_bluetooth.setDiscovering(true);
    m_bluetooth.setDiscoverable(true);
}

void BluetoothPage::hideEvent(QHideEvent* event)
{
    Page::hideEvent(event);
    m_bluetooth.setDiscovering(false);
    m_bluetooth.setDiscoverable(false);
}

QWidget* BluetoothPage::deviceControls(const shell::BluetoothDevice& device)
{
    auto* controls = new QWidget(this);
    auto* row = new QHBoxLayout(controls);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    const QString path = device.path;
    if (!device.paired) {
        const bool pairing = m_pairing.contains(path);
        auto* pair = new QPushButton(pairing ? u"Pairing…"_s : u"Pair"_s, controls);
        pair->setEnabled(!pairing);
        connect(pair, &QPushButton::clicked, this, [this, path, pair] {
            pair->setEnabled(false);
            pair->setText(u"Pairing…"_s);
            m_pairing.insert(path);
            m_bluetooth.pair(path);
        });
        row->addWidget(pair);
        return controls;
    }
    auto* toggle = new QPushButton(device.connected ? u"Disconnect"_s : u"Connect"_s, controls);
    connect(toggle, &QPushButton::clicked, this, [this, path, toggle, connected = device.connected] {
        toggle->setEnabled(false);
        if (connected)
            m_bluetooth.disconnectDevice(path);
        else
            m_bluetooth.connectDevice(path);
    });
    auto* forget = new QPushButton(u"Forget"_s, controls);
    connect(forget, &QPushButton::clicked, this, [this, path, name = device.name] {
        if (tde::Dialog::confirm(window(), u"Forget Device"_s, u"Forget %1?"_s.arg(name),
                u"It has to be paired again to be used."_s, u"Forget"_s))
            m_bluetooth.remove(path);
    });
    row->addWidget(toggle);
    row->addWidget(forget);
    return controls;
}

void BluetoothPage::sync()
{
    const bool powered = m_bluetooth.isPowered();
    if (m_bluetooth.isAvailable() && powered == m_shownPowered && m_bluetooth.devices() == m_shown
        && !m_content->layout()->isEmpty())
        return;
    m_shown = m_bluetooth.devices();
    m_shownPowered = powered;
    for (const shell::BluetoothDevice& device : m_shown) {
        if (device.paired)
            m_pairing.remove(device.path);
    }
    // Turned on while the page shows.
    if (isVisible()) {
        m_bluetooth.setDiscovering(true);
        m_bluetooth.setDiscoverable(true);
    }

    QLayout* layout = m_content->layout();
    shell::clearLayout(*layout);

    auto* top = new Group({}, m_content);
    layout->addWidget(top);
    if (!m_bluetooth.isAvailable()) {
        top->addRow(u"No Bluetooth"_s, u"No adapter was found, or the Bluetooth service does not run"_s);
        return;
    }
    auto* power = new Switch(m_content);
    power->setChecked(powered);
    connect(power, &Switch::toggled, this, [this](bool on) { m_bluetooth.setPowered(on); });
    top->addRow(u"Bluetooth"_s,
        powered ? u"Visible to other devices as “%1” while this page is open"_s.arg(m_bluetooth.name())
                : u"Turned off"_s,
        power);
    if (!powered)
        return;

    auto* paired = new Group(u"Devices"_s, m_content);
    auto* nearby = new Group(u"Nearby Devices"_s, m_content);
    auto* searching = new QLabel(u"Searching…"_s, nearby);
    searching->setEnabled(false);
    nearby->setHeaderWidget(searching);
    bool anyPaired = false;
    bool anyNearby = false;
    for (const shell::BluetoothDevice& device : m_shown) {
        QStringList state;
        if (device.paired)
            state << (device.connected ? u"Connected"_s : u"Not connected"_s);
        if (device.battery >= 0)
            state << u"%1% battery"_s.arg(device.battery);
        (device.paired ? paired : nearby)
            ->addRow(
                device.name.isEmpty() ? device.address : device.name, state.join(u" · "_s), deviceControls(device));
        (device.paired ? anyPaired : anyNearby) = true;
    }
    if (!anyPaired)
        paired->addNote(u"No devices are paired yet. Those nearby can be paired below."_s);
    if (!anyNearby)
        nearby->addNote(u"Make the device ready to pair, as its manual says, and it shows here."_s);
    layout->addWidget(paired);
    layout->addWidget(nearby);
}

} // namespace daedalus
