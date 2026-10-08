#include "PowerPage.hpp"

#include <QComboBox>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>

#include <algorithm>
#include <optional>
#include <vector>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

const QString Properties = u"org.freedesktop.DBus.Properties"_s;

// power-profiles-daemon, by the name it has now or the one it had before.
struct Profiles {
    QString service;
    QString path;
    QString interface;
};

std::optional<Profiles> powerProfiles()
{
    const auto bus = QDBusConnection::systemBus();
    for (const Profiles& profiles : {
             Profiles {u"org.freedesktop.UPower.PowerProfiles"_s, u"/org/freedesktop/UPower/PowerProfiles"_s,
                 u"org.freedesktop.UPower.PowerProfiles"_s},
             Profiles {u"net.hadess.PowerProfiles"_s, u"/net/hadess/PowerProfiles"_s, u"net.hadess.PowerProfiles"_s},
         }) {
        if (bus.interface()->isServiceRegistered(profiles.service)
            || bus.interface()->activatableServiceNames().value().contains(profiles.service))
            return profiles;
    }
    return std::nullopt;
}

QVariant busProperty(const QString& service, const QString& path, const QString& interface, const QString& name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, Properties, u"Get"_s);
    call << interface << name;
    const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(call);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

QString minutesText(int minutes)
{
    if (minutes % 60 == 0 && minutes >= 60)
        return minutes == 60 ? u"1 hour"_s : u"%1 hours"_s.arg(minutes / 60);
    return minutes == 1 ? u"1 minute"_s : u"%1 minutes"_s.arg(minutes);
}

// A choice of times, with `never` for 0 and whatever the config says besides.
QComboBox* times(std::vector<int> choices, int current, const QString& never, QWidget* parent)
{
    if (current > 0 && !std::ranges::contains(choices, current))
        choices.insert(std::ranges::upper_bound(choices, current), current);
    auto* box = new QComboBox(parent);
    box->addItem(never, 0);
    for (const int minutes : choices)
        box->addItem(minutesText(minutes), minutes);
    box->setCurrentIndex(std::max(0, box->findData(current)));
    return box;
}

} // namespace

PowerPage::PowerPage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    auto& power = settings.config.power;

    if (const auto profiles = powerProfiles()) {
        auto* mode = new QComboBox(this);
        const QString active
            = busProperty(profiles->service, profiles->path, profiles->interface, u"ActiveProfile"_s).toString();
        const QList<QVariantMap> offered = qdbus_cast<QList<QVariantMap>>(
            busProperty(profiles->service, profiles->path, profiles->interface, u"Profiles"_s));
        const std::pair<const char*, const char*> names[] = {
            {"performance", "Performance"},
            {"balanced", "Balanced"},
            {"power-saver", "Power Saver"},
        };
        for (const auto& [profile, label] : names) {
            const QString name = QString::fromLatin1(profile);
            if (std::ranges::any_of(offered, [&](const QVariantMap& p) { return p.value(u"Profile"_s) == name; }))
                mode->addItem(QString::fromUtf8(label), name);
        }
        mode->setCurrentIndex(std::max(0, mode->findData(active)));
        connect(mode, &QComboBox::activated, this, [mode, profiles = *profiles] {
            QDBusMessage call = QDBusMessage::createMethodCall(profiles.service, profiles.path, Properties, u"Set"_s);
            call << profiles.interface << u"ActiveProfile"_s << QVariant::fromValue(QDBusVariant(mode->currentData()));
            call.setInteractiveAuthorizationAllowed(true);
            QDBusConnection::systemBus().call(call, QDBus::NoBlock);
        });
        Group* modes = addGroup(u"Power Mode"_s);
        modes->addRow(u"Power mode"_s, u"Faster, or longer on battery"_s, mode);
    }

    Group* saving = addGroup(u"Power Saving"_s);
    QComboBox* blank = times({1, 2, 3, 4, 5, 8, 10, 12, 15}, power.blank, u"Never"_s, this);
    connect(blank, &QComboBox::activated, this, [&settings, blank] {
        settings.config.power.blank = blank->currentData().toInt();
        settings.save();
    });
    saving->addRow(u"Blank screen"_s, u"Turns the screens off after a while without input"_s, blank);

    const std::vector<int> suspendChoices {15, 20, 25, 30, 45, 60, 80, 90, 100, 120};
    QComboBox* pluggedIn = times(suspendChoices, power.suspend, u"Off"_s, this);
    connect(pluggedIn, &QComboBox::activated, this, [&settings, pluggedIn] {
        settings.config.power.suspend = pluggedIn->currentData().toInt();
        settings.save();
    });
    // A computer without a battery is always plugged in.
    const bool battery = busProperty(u"org.freedesktop.UPower"_s, u"/org/freedesktop/UPower/devices/DisplayDevice"_s,
        u"org.freedesktop.UPower.Device"_s, u"IsPresent"_s)
                             .toBool();
    saving->addRow(battery ? u"Automatic suspend when plugged in"_s : u"Automatic suspend"_s,
        u"Puts the computer to sleep after a while without input"_s, pluggedIn);
    if (battery) {
        QComboBox* onBattery = times(suspendChoices, power.suspendOnBattery, u"Off"_s, this);
        connect(onBattery, &QComboBox::activated, this, [&settings, onBattery] {
            settings.config.power.suspendOnBattery = onBattery->currentData().toInt();
            settings.save();
        });
        saving->addRow(u"Automatic suspend on battery"_s, {}, onBattery);
    }
    saving->addNote(u"Programs such as video players keep the screens on, and the computer awake, while they play."_s);
}

} // namespace daedalus
