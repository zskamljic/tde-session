#pragma once

#include "Picker.hpp"
#include <Tween.hpp>

#include <QPointF>
#include <QTransform>

#include <vector>

namespace argus {

class Canvas;
class Wallpaper;

// Where a window is drawn: around its centre, as wide as given, turned about the vertical axis
// and tilted about the horizontal one.
struct Pose {
    QPointF center;
    double width = 0;
    double angle = 0; // degrees
    double tilt = 0; // degrees
    double opacity = 1;
};

// Flip 3D, as Windows 7 had it, on Super+Tab: the windows on an arc leading back into the
// screen, the one picked in front at the bottom right. They fly there from where they are, and
// back as it closes.
class Flip : public Picker {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Argus.Flip")

public:
    Flip(Toplevels& toplevels, const Wallpaper& wallpaper, QWidget* parent = nullptr);

    // How long it takes to open or close, and to turn to the next window, in milliseconds.
    void setAnimationTimes(int show, int step)
    {
        m_showTime = show;
        m_stepTime = step;
    }

public slots:
    Q_SCRIPTABLE void Show(bool backwards, bool sameApplication) { pick(backwards, sameApplication); }
    Q_SCRIPTABLE void Release() { release(); }

protected:
    void opened() override;
    void moved() override;
    void closing() override;

    void resizeEvent(QResizeEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Card {
        int index = 0;
        double position = 0; // 0 in front, then further back
        double opacity = 1;
    };

    // The windows over the desktop, on the canvas, which redraw() has drawn again.
    void paint(QPainter& painter);
    void redraw();
    // The cards in the order they are drawn, from the back.
    std::vector<Card> cards() const;
    QSizeF sizeOf(const Toplevel& window) const; // drawn in front
    Pose stacked(double position, const QSizeF& size) const;
    Pose poseOf(const Card& card, const Toplevel& window) const;
    QTransform transform(const Pose& pose, const QSizeF& size) const;
    int cardAt(QPointF pos) const;

    const Wallpaper& m_wallpaper;
    Canvas* m_canvas = nullptr;
    shell::Tween m_offset; // how far it flipped, following the moves
    shell::Tween m_shown; // from 0 with the windows where they are, to 1 with them stacked
    int m_showTime = 300;
    int m_stepTime = 220;
};

} // namespace argus
