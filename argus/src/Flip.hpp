#pragma once

#include "Toplevels.hpp"

#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QTransform>
#include <QVariantAnimation>
#include <QWidget>

#include <map>
#include <vector>

namespace argus {

// Where a window is drawn: around its centre, as wide as given, turned away about the
// vertical axis.
struct Pose {
    QPointF center;
    double width = 0;
    double angle = 0; // degrees
    double opacity = 1;
};

// Flip 3D, as Windows 7 had it: the windows in a row leading back into the screen, most
// recently used in front. Super+Tab brings it up, and every Tab sends the window in front to
// the back; letting go of Super switches to the window in front. Escape goes back to the
// window that was used, and a click picks any.
class Flip : public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Argus.Flip")

public:
    explicit Flip(Toplevels& toplevels, QWidget* parent = nullptr);

public slots:
    // Shows the windows with the next one in front, or flips to the next one when shown.
    Q_SCRIPTABLE void Show(bool backwards);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Card {
        int index = 0; // in m_order
        double position = 0;
        double opacity = 1;
    };

    void appear();
    void step(int by);
    // Closes onto the window at `index` of m_order, or onto the one used last when -1.
    void choose(int index);
    void animate(QVariantAnimation& animation, double from, double to, int duration);
    void cacheImage(const Toplevel& window);

    const Toplevel* window(int index) const;
    int frontIndex() const;
    // The cards in the order they are drawn, from the back.
    std::vector<Card> cards() const;
    QSizeF sizeOf(const Toplevel& window) const; // drawn in front
    Pose stacked(double position, const QSizeF& size) const;
    Pose poseOf(const Card& card, const Toplevel& window) const;
    QTransform transform(const Pose& pose, const QSizeF& size) const;
    int cardAt(QPointF pos) const;

    Toplevels& m_toplevels;
    std::vector<quint64> m_order; // most recently used first
    std::map<quint64, QImage> m_images; // the pictures, scaled to their size in front

    int m_target = 0; // how many times it flipped, backwards below 0
    double m_offset = 0; // the same, as far as the animation got
    QVariantAnimation m_flipping;
    double m_shown = 0; // from 0 with the windows where they are, to 1 with them stacked
    QVariantAnimation m_showing;

    bool m_opening = false; // waiting for the pictures and places of the windows
    bool m_closing = false;
    int m_pendingSteps = 0; // flips asked for while opening
    quint64 m_chosen = 0;
    QTimer m_patience;
};

} // namespace argus
