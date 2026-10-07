#pragma once

#include <QVariantAnimation>

#include <algorithm>

namespace shell {

// A number moving smoothly to where it is sent; valueChanged follows every step of the way.
class Tween : public QVariantAnimation {
public:
    explicit Tween(QObject* parent = nullptr)
        : QVariantAnimation(parent)
    {
        setEasingCurve(QEasingCurve::OutCubic);
        connect(
            this, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) { m_now = value.toDouble(); });
    }

    double now() const { return m_now; }

    void jump(double value)
    {
        stop();
        m_now = value;
    }

    // From where it is to `target` in `duration` ms.
    void go(double target, int duration)
    {
        stop();
        setDuration(std::max(1, duration));
        setStartValue(m_now);
        setEndValue(target);
        start();
    }

private:
    double m_now = 0;
};

} // namespace shell
