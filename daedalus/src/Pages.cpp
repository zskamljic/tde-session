#include "Pages.hpp"

#include <tde/Theme.hpp>

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

// Keys as printed on them, in a box each.
QLabel* keys(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(u"Keys"_s);
    return label;
}

QSpinBox* milliseconds(int value, QWidget* parent)
{
    auto* box = new QSpinBox(parent);
    box->setRange(0, 2000);
    box->setSingleStep(50);
    box->setSuffix(u" ms"_s);
    box->setSpecialValueText(u"Off"_s);
    box->setValue(value);
    return box;
}

} // namespace

WindowsPage::WindowsPage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    using Style = shell::SessionConfig::Switcher::Style;
    auto& config = settings.config;

    auto* style = new QComboBox(this);
    style->addItem(u"Pictures of the windows"_s, int(Style::Previews));
    style->addItem(u"Application icons"_s, int(Style::Icons));
    style->setCurrentIndex(style->findData(int(config.switcher.style)));
    connect(style, &QComboBox::activated, this, [&settings, style] {
        settings.config.switcher.style = Style(style->currentData().toInt());
        settings.save();
    });
    Group* switching = addGroup(u"Switching Windows"_s);
    switching->addRow(u"Alt+Tab shows"_s, u"What the switcher shows of every window"_s, style);

    Group* animations = addGroup(u"Animations"_s);
    const auto duration = [&](const QString& title, const QString& subtitle, int& value) {
        QSpinBox* box = milliseconds(value, this);
        connect(box, &QSpinBox::valueChanged, this, [&settings, &value](int changed) {
            value = changed;
            settings.save();
        });
        animations->addRow(title, subtitle, box);
    };
    duration(u"Overview"_s, u"Windows moving in and out of the overview"_s, config.animations.overview);
    duration(u"Flip 3D"_s, u"Windows moving into the stack and back"_s, config.animations.flip);
    duration(u"Flip 3D turning"_s, u"The next window coming to the front"_s, config.animations.flipStep);
    duration(u"Tiling"_s, u"Windows growing to fill the screen or half of it"_s, config.animations.windows);

    Group* shortcuts = addGroup(u"Keyboard Shortcuts"_s);
    shortcuts->addRow(u"Switch windows"_s, {}, keys(u"Alt+Tab"_s, this));
    shortcuts->addRow(u"Switch windows of an application"_s, {}, keys(u"Alt+`"_s, this));
    shortcuts->addRow(u"Flip through windows in 3D"_s, {}, keys(u"Super+Tab"_s, this));
    shortcuts->addRow(u"Show all windows"_s, {}, keys(u"Super"_s, this));
    shortcuts->addNote(u"Shift goes backwards; letting go of Alt or Super switches to the window picked."_s);
}

DateTimePage::DateTimePage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    auto* seconds = new Switch(this);
    seconds->setChecked(settings.config.clock.seconds);
    connect(seconds, &Switch::toggled, this, [&settings](bool on) {
        settings.config.clock.seconds = on;
        settings.save();
    });
    Group* clock = addGroup(u"Clock"_s);
    clock->addRow(u"Seconds"_s, u"In the clock of the bar and of the lock screen"_s, seconds);
}

} // namespace daedalus
