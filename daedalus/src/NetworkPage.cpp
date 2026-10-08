#include "NetworkPage.hpp"

#include <WifiPassword.hpp>
#include <tde/Dialog.hpp>
#include <tde/Toast.hpp>

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace daedalus {

NetworkPage::NetworkPage(QWidget* parent)
    : Page(parent)
    , m_toast(new tde::Toast(this))
{
    m_content = new QWidget(this);
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);
    addWidget(m_content);

    m_scanning.setInterval(15000);
    connect(&m_scanning, &QTimer::timeout, &m_wifi, &shell::Wifi::scan);
    connect(&m_wifi, &shell::Wifi::changed, this, &NetworkPage::sync);
    connect(&m_wifi, &shell::Wifi::failed, this, [this](const QString& message) {
        const QString network = m_wifi.connecting();
        m_toast->showMessage(network.isEmpty() ? message : u"%1: %2"_s.arg(network, message), 6000);
    });
    sync();
}

void NetworkPage::showEvent(QShowEvent* event)
{
    Page::showEvent(event);
    m_wifi.scan();
    m_scanning.start();
}

void NetworkPage::hideEvent(QHideEvent* event)
{
    Page::hideEvent(event);
    m_scanning.stop();
}

QWidget* NetworkPage::networkControls(const shell::WifiNetwork& network)
{
    auto* controls = new QWidget(this);
    auto* row = new QHBoxLayout(controls);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    const QString ssid = network.ssid;
    if (network.strength >= 0) {
        auto* toggle = new QPushButton(network.active ? u"Disconnect"_s : u"Connect"_s, controls);
        connect(toggle, &QPushButton::clicked, this,
            [this, ssid, active = network.active, needs = network.needsPassword()] {
                if (active) {
                    m_wifi.disconnect();
                } else if (!needs) {
                    m_wifi.connectTo(ssid);
                } else if (const auto password = shell::askWifiPassword(window(), ssid)) {
                    m_wifi.connectTo(ssid, *password);
                }
            });
        row->addWidget(toggle);
    }
    if (!network.connection.isEmpty()) {
        auto* forget = new QPushButton(u"Forget"_s, controls);
        connect(forget, &QPushButton::clicked, this, [this, ssid] {
            if (tde::Dialog::confirm(window(), u"Forget Network"_s, u"Forget “%1”?"_s.arg(ssid),
                    u"Its password is forgotten too, and the computer no longer connects to it by itself."_s,
                    u"Forget"_s))
                m_wifi.forget(ssid);
        });
        row->addWidget(forget);
    }
    return controls;
}

void NetworkPage::sync()
{
    if (m_built && m_wifi.networks() == m_shownNetworks && m_wifi.wired() == m_shownWired
        && m_wifi.isEnabled() == m_shownEnabled)
        return;
    m_built = true;
    m_shownNetworks = m_wifi.networks();
    m_shownWired = m_wifi.wired();
    m_shownEnabled = m_wifi.isEnabled();

    // Later, as the button that asked for this may be among them.
    QLayout* layout = m_content->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    if (!m_wifi.isAvailable() && m_shownWired.empty()) {
        auto* none = new Group({}, m_content);
        none->addRow(u"No Network Devices"_s, u"NetworkManager does not run, or has no devices to manage"_s);
        layout->addWidget(none);
        return;
    }

    if (!m_shownWired.empty()) {
        auto* wired = new Group(u"Wired"_s, m_content);
        for (const shell::WiredDevice& device : m_shownWired) {
            wired->addRow(m_shownWired.size() == 1 ? u"Cable"_s : device.name,
                device.connected       ? u"Connected"_s
                    : device.pluggedIn ? u"Not connected"_s
                                       : u"Cable unplugged"_s);
        }
        layout->addWidget(wired);
    }
    if (!m_wifi.isAvailable())
        return;

    auto* wifi = new Group(u"Wi-Fi"_s, m_content);
    auto* power = new Switch(wifi);
    power->setChecked(m_shownEnabled);
    connect(power, &Switch::toggled, this, [this](bool on) { m_wifi.setEnabled(on); });
    wifi->addRow(u"Wi-Fi"_s, m_shownEnabled ? QString() : u"Turned off"_s, power);
    layout->addWidget(wifi);
    if (!m_shownEnabled)
        return;

    auto* visible = new Group(u"Networks in Range"_s, m_content);
    auto* known = new Group(u"Known Networks"_s, m_content);
    bool anyVisible = false;
    bool anyKnown = false;
    for (const shell::WifiNetwork& network : m_shownNetworks) {
        if (network.strength >= 0) {
            QStringList state;
            if (network.active)
                state << u"Connected"_s;
            else if (network.passwordRejected)
                state << u"The password did not work"_s;
            else if (!network.connection.isEmpty())
                state << u"Known"_s;
            if (network.secured)
                state << u"Secured"_s;
            state << u"Signal %1%"_s.arg(network.strength);
            visible->addRow(network.ssid, state.join(u" · "_s), networkControls(network));
            anyVisible = true;
        } else {
            known->addRow(network.ssid, u"Out of range"_s, networkControls(network));
            anyKnown = true;
        }
    }
    if (!anyVisible)
        visible->addNote(u"No networks are in range."_s);
    layout->addWidget(visible);
    if (anyKnown)
        layout->addWidget(known);
    else
        known->deleteLater();
}

} // namespace daedalus
