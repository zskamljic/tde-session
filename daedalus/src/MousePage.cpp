#include "MousePage.hpp"

#include <QFile>
#include <QSlider>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

// From slow to fast, with a mark at the usual speed.
QSlider* slider(int minimum, int maximum, int usual, int value, QWidget* parent)
{
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    slider->setSingleStep(5);
    slider->setPageStep(25);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(usual - minimum);
    slider->setFixedWidth(220);
    return slider;
}

} // namespace

bool hasTouchpad()
{
    QFile devices(u"/proc/bus/input/devices"_s);
    if (!devices.open(QIODevice::ReadOnly | QIODevice::Text))
        return true; // not known: offered all the same
    const QByteArray text = devices.readAll().toLower();
    return text.contains("touchpad") || text.contains("trackpad");
}

MousePage::MousePage(Settings& settings, QWidget* parent)
    : Page(parent)
{
    using Pointer = shell::SessionConfig::Input::Pointer;
    // The rows every kind of pointing device has.
    const auto common = [&](Group* group, Pointer& pointer, const QString& scrolling) {
        QSlider* speed = slider(-100, 100, 0, pointer.speed, this);
        connect(speed, &QSlider::valueChanged, this, [&settings, &pointer](int value) {
            pointer.speed = value;
            settings.save();
        });
        group->addRow(u"Pointer Speed"_s, u"How far the pointer goes for a move of the hand"_s, speed);

        QSlider* scroll = slider(10, 190, 100, pointer.scrollSpeed, this);
        connect(scroll, &QSlider::valueChanged, this, [&settings, &pointer](int value) {
            pointer.scrollSpeed = value;
            settings.save();
        });
        group->addRow(u"Scroll Speed"_s, scrolling, scroll);

        auto* natural = new Switch(this);
        natural->setChecked(pointer.naturalScroll);
        connect(natural, &Switch::toggled, this, [&settings, &pointer](bool on) {
            pointer.naturalScroll = on;
            settings.save();
        });
        group->addRow(u"Natural Scrolling"_s, u"The content moves the way the fingers or the wheel go"_s, natural);
    };

    Group* mouse = addGroup(u"Mouse"_s);
    common(mouse, settings.config.input.mouse,
        u"How far a turn of the wheel scrolls; lower it for wheels that spin freely"_s);

    if (!hasTouchpad())
        return;
    Pointer& pad = settings.config.input.touchpad;
    Group* touchpad = addGroup(u"Touchpad"_s);
    common(touchpad, pad, u"How far a move of two fingers scrolls"_s);
    auto* tap = new Switch(this);
    tap->setChecked(pad.tapToClick);
    connect(tap, &Switch::toggled, this, [&settings, &pad](bool on) {
        pad.tapToClick = on;
        settings.save();
    });
    touchpad->addRow(u"Tap to Click"_s, u"A tap of one finger clicks, of two right-clicks"_s, tap);
    auto* typing = new Switch(this);
    typing->setChecked(pad.disableWhileTyping);
    connect(typing, &Switch::toggled, this, [&settings, &pad](bool on) {
        pad.disableWhileTyping = on;
        settings.save();
    });
    touchpad->addRow(u"Off While Typing"_s, u"The touchpad does nothing for a moment after a key"_s, typing);
}

} // namespace daedalus
