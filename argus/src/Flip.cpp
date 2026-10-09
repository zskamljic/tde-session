#include "Flip.hpp"

#include "Background.hpp"
#include "Canvas.hpp"

#include <tde/Theme.hpp>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

constexpr double Angle = -35; // how far the windows are turned to face right
constexpr double Tilt = 12; // and up
constexpr double Bend = -5; // degrees more for each window further back
constexpr double Shrink = 0.9; // each window further back is this much smaller
constexpr double Visible = 6; // windows further back than this fade away
constexpr int Radius = 8;
constexpr int Glow = 6; // rings of light around each window
constexpr int GlowAlpha = 10; // of the innermost ring
const QSizeF PlaceholderSize(480, 320);

// The pose part of the way from `from` to `to`.
Pose interpolate(const Pose& from, const Pose& to, double t)
{
    return Pose {
        .center = from.center + (to.center - from.center) * t,
        .width = from.width + (to.width - from.width) * t,
        .angle = from.angle + (to.angle - from.angle) * t,
        .tilt = from.tilt + (to.tilt - from.tilt) * t,
        .opacity = from.opacity + (to.opacity - from.opacity) * t,
    };
}

// Where in the stack the window at `index` is when it flipped `offset` times through `count`
// windows: 0 in front, then further back. Between -1 and 0 the window leaves the front, on its
// way to the back. A window alone stays in front: there is nothing to flip to.
double stackPosition(int index, double offset, int count)
{
    if (count <= 1)
        return 0;
    double position = std::fmod(index - offset + 1, count);
    if (position < 0)
        position += count;
    return position - 1;
}

} // namespace

Flip::Flip(Toplevels& toplevels, const Wallpaper& wallpaper, QWidget* parent)
    : Picker(toplevels, u"tde-argus-flip"_s, parent)
    , m_wallpaper(wallpaper)
{
    setWindowTitle(u"Windows"_s);
    m_canvas = new Canvas(this, [this](QPainter& painter) { paint(painter); });
    connect(&m_offset, &QVariantAnimation::valueChanged, this, &Flip::redraw);
    connect(&m_shown, &QVariantAnimation::valueChanged, this, &Flip::redraw);
    connect(&m_shown, &QVariantAnimation::finished, this, [this] {
        if (isClosing())
            finish();
    });
    connect(&m_toplevels, &Toplevels::previewChanged, this, [this] {
        if (isVisible())
            redraw();
    });
}

void Flip::redraw()
{
    m_canvas->update();
}

void Flip::resizeEvent(QResizeEvent* event)
{
    Picker::resizeEvent(event);
    m_canvas->setGeometry(rect());
}

void Flip::opened()
{
    m_offset.jump(0);
    m_shown.jump(0);
    m_shown.go(1, m_showTime);
}

void Flip::moved()
{
    m_offset.go(moves(), m_stepTime);
}

void Flip::closing()
{
    m_offset.stop();
    m_shown.go(0, int(m_showTime * m_shown.now()));
}

std::vector<Flip::Card> Flip::cards() const
{
    std::vector<Card> cards;
    for (int i = 0; i < count(); ++i) {
        const double position = stackPosition(i, m_offset.now(), count());
        if (position < 0) {
            // Leaving the front, fading as it goes, and coming in at the back.
            cards.push_back(Card {.index = i, .position = position, .opacity = 1 + position});
            cards.push_back(Card {.index = i, .position = position + count(), .opacity = -position});
        } else {
            cards.push_back(Card {.index = i, .position = position, .opacity = 1});
        }
    }
    std::ranges::stable_sort(cards, [](const Card& a, const Card& b) { return a.position > b.position; });
    return cards;
}

QSizeF Flip::sizeOf(const Toplevel& window) const
{
    QSizeF size = window.preview.isNull() ? PlaceholderSize : QSizeF(window.preview.size()) / devicePixelRatioF();
    if (size.isEmpty())
        size = PlaceholderSize;
    const QSizeF room(width() * 0.5, height() * 0.5);
    if (size.width() > room.width() || size.height() > room.height())
        size.scale(room, Qt::KeepAspectRatio);
    return size;
}

Pose Flip::stacked(double position, const QSizeF& size) const
{
    // In front a little right of and below the middle, the others back to the top left along
    // an arc: up at first, then more to the side, turning as it bends.
    const QPointF along(
        -0.075 * position + 0.0025 * position * position, -0.07 * position + 0.007 * position * position);
    return Pose {
        .center = QPointF(width() * (0.64 + along.x()), height() * (0.6 + along.y())),
        .width = size.width() * std::pow(Shrink, position),
        .angle = Angle + Bend * position,
        .tilt = Tilt,
        .opacity = std::clamp(Visible + 1 - position, 0.0, 1.0),
    };
}

Pose Flip::poseOf(const Card& card, const Toplevel& window) const
{
    Pose inStack = stacked(card.position, sizeOf(window));
    inStack.opacity *= card.opacity;
    // Between where the window is on the screen and its place in the stack, both ways, so
    // every window is back where it is as it closes; one that is not there fades in place.
    Pose resting = inStack;
    resting.opacity = 0;
    if (const QRectF frame = placeOf(window, *this); !frame.isEmpty())
        resting = Pose {.center = frame.center(), .width = frame.width()};
    return interpolate(resting, inStack, m_shown.now());
}

QTransform Flip::transform(const Pose& pose, const QSizeF& size) const
{
    QTransform transform;
    transform.translate(pose.center.x(), pose.center.y());
    transform.rotate(pose.angle, Qt::YAxis);
    transform.rotate(pose.tilt, Qt::XAxis);
    const double scale = pose.width / size.width();
    transform.scale(scale, scale);
    return transform;
}

void Flip::paint(QPainter& painter)
{
    const auto& colors = tde::theme::colors();
    const double shown = m_shown.now();
    paintBackdrop(painter, *this, m_wallpaper, shown);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    std::vector<Card> order = cards();
    // Closing, they are stacked as they will be on the screen, the least recently used at the
    // bottom and the one it closes onto on top, so nothing jumps as they arrive.
    if (isClosing()) {
        std::ranges::stable_sort(order, [](const Card& a, const Card& b) { return a.index > b.index; });
        std::ranges::stable_partition(order, [this](const Card& card) {
            const Toplevel* shown = window(card.index);
            return !shown || shown->id != chosen();
        });
    }
    for (const Card& card : order) {
        const Toplevel* shownWindow = window(card.index);
        if (!shownWindow)
            continue;
        const Pose pose = poseOf(card, *shownWindow);
        if (pose.opacity <= 0)
            continue;
        const QSizeF size = sizeOf(*shownWindow);
        const QRectF area(QPointF(-size.width() / 2, -size.height() / 2), size);
        painter.setTransform(transform(pose, size));
        painter.setOpacity(pose.opacity);

        QPainterPath shape;
        shape.addRoundedRect(area, Radius * shown, Radius * shown);
        // A soft light around it, fading out.
        painter.setBrush(Qt::NoBrush);
        for (int ring = Glow; ring > 0; --ring) {
            painter.setPen(QPen(QColor(255, 255, 255, int(GlowAlpha * (Glow + 1 - ring) / Glow * shown)), ring * 3.0));
            painter.drawPath(shape);
        }
        // The picture as it was taken, turned in one go, which keeps it sharp.
        if (!shownWindow->preview.isNull()) {
            painter.save();
            painter.setClipPath(shape);
            painter.drawImage(area, shownWindow->preview);
            painter.restore();
        } else {
            paintPlaceholder(painter, shape, m_toplevels.iconOf(*shownWindow));
        }
        painter.setPen(QPen(colors.border, 1));
        painter.drawPath(shape);
    }
    painter.resetTransform();

    // What the window in front is called, at the bottom.
    if (const Toplevel* front = window(picked()); front && !isClosing()) {
        painter.setOpacity(shown);
        QFont font = painter.font();
        font.setPointSizeF(font.pointSizeF() * 1.3);
        painter.setFont(font);
        painter.setPen(colors.text);
        const QFontMetrics metrics = painter.fontMetrics();
        const QRect line(0, height() - metrics.height() - 24, width(), metrics.height());
        painter.drawText(
            line, Qt::AlignCenter, metrics.elidedText(front->displayName(), Qt::ElideMiddle, width() - 64));
    }
}

int Flip::cardAt(QPointF pos) const
{
    const std::vector<Card> order = cards();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Toplevel* shown = window(it->index);
        if (!shown)
            continue;
        const Pose pose = poseOf(*it, *shown);
        if (pose.opacity < 0.5)
            continue;
        const QSizeF size = sizeOf(*shown);
        const QPolygonF outline
            = transform(pose, size).map(QRectF(QPointF(-size.width() / 2, -size.height() / 2), size));
        if (outline.containsPoint(pos, Qt::OddEvenFill))
            return it->index;
    }
    return -1;
}

void Flip::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        choose(cardAt(event->position()));
}

void Flip::wheelEvent(QWheelEvent* event)
{
    if (const int delta = event->angleDelta().y(); delta != 0)
        move(delta > 0 ? -1 : 1);
}

} // namespace argus
