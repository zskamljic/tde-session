#include "PowerPage.hpp"

#include <BusProperties.hpp>

#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLabel>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

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

const QString UPower = u"org.freedesktop.UPower"_s;
const QString DeviceInterface = u"org.freedesktop.UPower.Device"_s;

QVariantMap deviceProperties(const QString& path)
{
    return shell::allProperties(QDBusConnection::systemBus(), UPower, path, DeviceInterface);
}

// The batteries of the computer itself, not those of mice and headphones.
QList<QVariantMap> batteries()
{
    QList<QVariantMap> found;
    const QDBusReply<QList<QDBusObjectPath>> devices = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall(UPower, u"/org/freedesktop/UPower"_s, UPower, u"EnumerateDevices"_s));
    if (!devices.isValid())
        return found;
    for (const QDBusObjectPath& path : devices.value()) {
        QVariantMap properties = deviceProperties(path.path());
        if (properties.value(u"Type"_s).toUInt() == 2 && properties.value(u"PowerSupply"_s).toBool()
            && properties.value(u"IsPresent"_s).toBool())
            found << properties;
    }
    return found;
}

QString duration(qint64 seconds)
{
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60)
        return minutes == 1 ? u"1 minute"_s : u"%1 minutes"_s.arg(minutes);
    return u"%1 h %2 min"_s.arg(minutes / 60).arg(minutes % 60);
}

QString stateText(const QVariantMap& battery)
{
    switch (battery.value(u"State"_s).toUInt()) {
    case 1:
        if (const qint64 full = battery.value(u"TimeToFull"_s).toLongLong(); full > 0)
            return u"Charging, full in %1"_s.arg(duration(full));
        return u"Charging"_s;
    case 2:
        if (const qint64 empty = battery.value(u"TimeToEmpty"_s).toLongLong(); empty > 0)
            return u"On battery, %1 left"_s.arg(duration(empty));
        return u"On battery"_s;
    case 3:
        return u"Empty"_s;
    case 4:
        return u"Fully charged"_s;
    case 5:
    case 6:
        return u"Plugged in, not charging"_s;
    default:
        return u"Unknown"_s;
    }
}

QLabel* valueLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

} // namespace

PowerPage::PowerPage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    auto& power = settings.config.power;

    // Each battery: how full, what it is doing, how worn and how much it gives.
    struct Shown {
        QString path; // as the kernel names it
        QLabel* charge;
        QLabel* state;
        QLabel* health;
        QLabel* rate;
    };
    std::vector<Shown> shown;
    const QList<QVariantMap> found = batteries();
    for (qsizetype i = 0; i < found.size(); ++i) {
        const QVariantMap& battery = found[i];
        Group* group = addGroup(found.size() == 1 ? u"Battery"_s : u"Battery %1"_s.arg(i + 1));
        const Shown& rows = shown.emplace_back(Shown {battery.value(u"NativePath"_s).toString(), valueLabel(this),
            valueLabel(this), valueLabel(this), valueLabel(this)});
        group->addRow(u"Charge"_s, {}, rows.charge);
        group->addRow(u"State"_s, {}, rows.state);
        group->addRow(u"Health"_s, u"How much it holds of what it did when new"_s, rows.health);
        group->addRow(u"Power"_s, u"Being drawn from it, or charged into it"_s, rows.rate);
        const QString model = QStringList {battery.value(u"Vendor"_s).toString(), battery.value(u"Model"_s).toString()}
                                  .join(u' ')
                                  .trimmed();
        if (!model.isEmpty()) {
            QLabel* label = valueLabel(this);
            label->setText(model);
            group->addRow(u"Model"_s, {}, label);
        }
    }
    if (!shown.empty()) {
        // As they are now, and again every few seconds while the page shows.
        const auto update = [this, shown] {
            if (!isVisible())
                return;
            for (const QVariantMap& battery : batteries()) {
                const auto rows = std::ranges::find(shown, battery.value(u"NativePath"_s).toString(), &Shown::path);
                if (rows == shown.end())
                    continue;
                rows->charge->setText(u"%1%"_s.arg(std::lround(battery.value(u"Percentage"_s).toDouble())));
                rows->state->setText(stateText(battery));
                const double capacity = battery.value(u"Capacity"_s).toDouble();
                rows->health->setText(capacity > 0 ? u"%1%"_s.arg(std::lround(capacity)) : u"Not known"_s);
                const double watts = battery.value(u"EnergyRate"_s).toDouble();
                rows->rate->setText(watts > 0 ? u"%1 W"_s.arg(watts, 0, 'f', 1) : u"None"_s);
            }
        };
        m_batteryUpdate = update;
        connect(&m_batteryTimer, &QTimer::timeout, this, update);
        m_batteryTimer.start(5000);
    }

    if (m_profiles.isAvailable()) {
        auto* mode = new QComboBox(this);
        const auto fill = [this, mode] {
            mode->clear();
            for (const QString& profile : m_profiles.offered())
                mode->addItem(shell::powerProfileName(profile), profile);
            mode->setCurrentIndex(std::max(0, mode->findData(m_profiles.active())));
        };
        fill();
        connect(&m_profiles, &shell::PowerProfiles::changed, mode, fill);
        connect(
            mode, &QComboBox::activated, this, [this, mode] { m_profiles.setActive(mode->currentData().toString()); });
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
    const bool battery = shell::property(QDBusConnection::systemBus(), UPower,
        u"/org/freedesktop/UPower/devices/DisplayDevice"_s, u"org.freedesktop.UPower.Device"_s, u"IsPresent"_s)
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

void PowerPage::showEvent(QShowEvent* event)
{
    Page::showEvent(event);
    if (m_batteryUpdate)
        m_batteryUpdate();
}

} // namespace daedalus
