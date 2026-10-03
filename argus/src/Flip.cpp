#include "Flip.hpp"

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

constexpr double Angle = -35; // how far the windows are turned, their right edge away
constexpr double Shrink = 0.86; // each window further back is this much smaller
constexpr double Visible = 6; // windows further back than this fade away
constexpr int Radius = 8;
constexpr int ShowTime = 300; // ms
constexpr int FlipTime = 220; // ms
constexpr int Patience = 150; // ms to wait for pictures of the windows before showing
constexpr int KeysSettle = 50; // ms
const QSizeF PlaceholderSize(480, 320);

// The pose part of the way from `from` to `to`.
Pose interpolate(const Pose& from, const Pose& to, double t)
{
    return Pose {
        .center = from.center + (to.center - from.center) * t,
        .width = from.width + (to.width - from.width) * t,
        .angle = from.angle + (to.angle - from.angle) * t,
        .opacity = from.opacity + (to.opacity - from.opacity) * t,
    };
}

// Where in the stack the window at `index` is when it flipped `offset` times through `count`
// windows: 0 in front, then further back. Between -1 and 0 the window leaves the front, on its
// way to the back.
double stackPosition(int index, double offset, int count)
{
    if (count <= 0)
        return 0;
    // Round and round: past the front, a window goes to the back.
    double position = std::fmod(index - offset + 1, count);
    if (position < 0)
        position += count;
    return position - 1;
}

} // namespace

Flip::Flip(Toplevels& toplevels, QWidget* parent)
    : QWidget(parent)
    , m_toplevels(toplevels)
{
    setWindowTitle(u"Windows"_s);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    // Over everything, covering the whole screen and taking the keyboard while shown.
    create();
    if (auto* layer = LayerShellQt::Window::get(windowHandle())) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layer->setScope(u"tde-argus-flip"_s);
    }

    m_flipping.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_flipping, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_offset = value.toDouble();
        update();
    });
    m_showing.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_showing, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_shown = value.toDouble();
        update();
    });
    connect(&m_showing, &QVariantAnimation::finished, this, [this] {
        if (m_closing) {
            m_closing = false;
            m_images.clear();
            hide();
        }
    });

    m_patience.setSingleShot(true);
    m_patience.setInterval(Patience);
    connect(&m_patience, &QTimer::timeout, this, &Flip::appear);
    connect(&m_toplevels, &Toplevels::refreshed, this, [this] {
        if (m_opening)
            appear();
    });
    connect(&m_toplevels, &Toplevels::previewChanged, this, [this](quint64 id) {
        if (!isVisible())
            return;
        for (const auto& window : m_toplevels.windows()) {
            if (window->id == id)
                cacheImage(*window);
        }
        update();
    });
}

void Flip::Show(bool backwards)
{
    const int by = backwards ? -1 : 1;
    if (m_closing)
        return;
    if (isVisible()) {
        step(by);
        return;
    }
    m_pendingSteps += by;
    if (m_opening)
        return;
    m_opening = true;
    m_patience.start();
    m_toplevels.refresh();
}

void Flip::appear()
{
    if (!m_opening)
        return;
    m_opening = false;
    m_patience.stop();

    std::vector<const Toplevel*> windows;
    for (const auto& window : m_toplevels.windows())
        windows.push_back(window.get());
    // Those not known to the compositor go last, in the order they opened.
    std::ranges::stable_sort(
        windows, [](const Toplevel* a, const Toplevel* b) { return unsigned(a->recency) < unsigned(b->recency); });
    m_order.clear();
    m_images.clear();
    for (const Toplevel* window : windows) {
        m_order.push_back(window->id);
        cacheImage(*window);
    }
    const int steps = std::exchange(m_pendingSteps, 0);
    if (m_order.empty())
        return;

    m_target = 0;
    m_offset = 0;
    m_shown = 0;
    show();
    setFocus();
    animate(m_showing, 0, 1, ShowTime);
    step(steps);
}

void Flip::step(int by)
{
    if (m_order.size() < 2 || by == 0)
        return;
    m_target += by;
    animate(m_flipping, m_offset, m_target, FlipTime);
}

void Flip::choose(int index)
{
    if (m_closing || m_order.empty())
        return;
    m_closing = true;
    m_flipping.stop();
    m_chosen = m_order[size_t(std::max(index, 0))];
    // It comes forward behind the windows shown here, which go back where they are.
    if (index >= 0)
        m_toplevels.activate(m_chosen);
    animate(m_showing, m_shown, 0, int(ShowTime * m_shown));
}

void Flip::animate(QVariantAnimation& animation, double from, double to, int duration)
{
    animation.stop();
    animation.setDuration(std::max(1, duration));
    animation.setStartValue(from);
    animation.setEndValue(to);
    animation.start();
}

void Flip::cacheImage(const Toplevel& window)
{
    if (window.preview.isNull())
        return;
    // Scaled once, so turning them each frame costs less.
    const QSize size = (sizeOf(window) * devicePixelRatioF()).toSize();
    m_images[window.id] = window.preview.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

const Toplevel* Flip::window(int index) const
{
    if (index < 0 || size_t(index) >= m_order.size())
        return nullptr;
    const auto it = std::ranges::find_if(
        m_toplevels.windows(), [&](const auto& window) { return window->id == m_order[size_t(index)]; });
    return it == m_toplevels.windows().end() ? nullptr : it->get();
}

int Flip::frontIndex() const
{
    const int count = int(m_order.size());
    return count == 0 ? -1 : ((m_target % count) + count) % count;
}

std::vector<Flip::Card> Flip::cards() const
{
    const int count = int(m_order.size());
    std::vector<Card> cards;
    for (int i = 0; i < count; ++i) {
        const double position = stackPosition(i, m_offset, count);
        if (position < 0) {
            // Leaving the front, fading as it goes, and coming in at the back.
            cards.push_back(Card {.index = i, .position = position, .opacity = 1 + position});
            if (count > 1)
                cards.push_back(Card {.index = i, .position = position + count, .opacity = -position});
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
    // In front a little left of and below the middle, the others back to the top right.
    const double scale = std::pow(Shrink, position);
    return Pose {
        .center = QPointF(width() * (0.36 + 0.11 * position), height() * (0.58 - 0.08 * position)),
        .width = size.width() * scale,
        .angle = Angle,
        .opacity = std::clamp(Visible + 1 - position, 0.0, 1.0),
    };
}

Pose Flip::poseOf(const Card& card, const Toplevel& window) const
{
    Pose inStack = stacked(card.position, sizeOf(window));
    inStack.opacity *= card.opacity;
    // Others than the chosen one fade where they are when it closes.
    if (m_closing && window.id != m_chosen) {
        inStack.opacity *= m_shown;
        return inStack;
    }
    // From where the window is on the screen; one that is not there fades in place.
    Pose resting = inStack;
    resting.opacity = 0;
    if (!window.frame.isEmpty() && !window.minimized) {
        const QRectF frame = window.frame.translated(-(screen() ? screen()->geometry().topLeft() : QPoint()));
        resting = Pose {.center = frame.center(), .width = frame.width(), .angle = 0, .opacity = 1};
    }
    return interpolate(resting, inStack, m_shown);
}

QTransform Flip::transform(const Pose& pose, const QSizeF& size) const
{
    QTransform transform;
    transform.translate(pose.center.x(), pose.center.y());
    transform.rotate(pose.angle, Qt::YAxis);
    const double scale = pose.width / size.width();
    transform.scale(scale, scale);
    return transform;
}

bool Flip::event(QEvent* event)
{
    // Super may have been let go before the keyboard came here; then the window in front is the
    // one wanted. The state of the keys comes along with the keyboard, a moment later.
    if (event->type() == QEvent::WindowActivate) {
        QTimer::singleShot(KeysSettle, this, [this] {
            if (isVisible() && !(QGuiApplication::queryKeyboardModifiers() & Qt::MetaModifier))
                choose(frontIndex());
        });
    }
    return QWidget::event(event);
}

void Flip::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QColor background = colors.header.darker(140);
    background.setAlphaF(float(m_shown));
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), background);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    std::vector<Card> order = cards();
    // The one it closes onto goes above the others.
    if (m_closing) {
        std::ranges::stable_partition(
            order, [this](const Card& card) { return m_order[size_t(card.index)] != m_chosen; });
    }
    for (const Card& card : order) {
        const Toplevel* shown = window(card.index);
        if (!shown)
            continue;
        const Pose pose = poseOf(card, *shown);
        if (pose.opacity <= 0)
            continue;
        const QSizeF size = sizeOf(*shown);
        const QRectF area(QPointF(-size.width() / 2, -size.height() / 2), size);
        painter.setTransform(transform(pose, size));
        painter.setOpacity(pose.opacity);

        QPainterPath shape;
        shape.addRoundedRect(area, Radius * m_shown, Radius * m_shown);
        const auto image = m_images.find(shown->id);
        if (image == m_images.end()) {
            painter.fillPath(shape, colors.window);
            const QIcon icon = shell::applicationIcon(nullptr, shown->appId.toLower());
            const double iconSize = std::min({96.0, size.width() / 2, size.height() / 2});
            icon.paint(&painter, QRectF(-iconSize / 2, -iconSize / 2, iconSize, iconSize).toRect());
        } else {
            painter.save();
            painter.setClipPath(shape);
            painter.drawImage(area, image->second);
            painter.restore();
        }
        painter.setPen(QPen(colors.border, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
    }
    painter.resetTransform();

    // What the window in front is called, below the stack.
    if (const Toplevel* front = window(frontIndex()); front && !m_closing) {
        painter.setOpacity(m_shown);
        QFont font = painter.font();
        font.setPointSizeF(font.pointSizeF() * 1.3);
        painter.setFont(font);
        painter.setPen(colors.text);
        const QString title = front->title.isEmpty() ? front->appId : front->title;
        const QRect line(
            0, height() - 2 * painter.fontMetrics().height() - 32, width(), painter.fontMetrics().height());
        painter.drawText(line, Qt::AlignCenter, painter.fontMetrics().elidedText(title, Qt::ElideMiddle, width() - 64));
    }
}

int Flip::cardAt(QPointF pos) const
{
    std::vector<Card> order = cards();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Toplevel* shown = window(it->index);
        if (!shown || poseOf(*it, *shown).opacity < 0.5)
            continue;
        const QSizeF size = sizeOf(*shown);
        const QPolygonF outline
            = transform(poseOf(*it, *shown), size).map(QRectF(QPointF(-size.width() / 2, -size.height() / 2), size));
        if (outline.containsPoint(pos, Qt::OddEvenFill))
            return it->index;
    }
    return -1;
}

void Flip::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Right:
    case Qt::Key_Down:
        step(1);
        break;
    case Qt::Key_Backtab:
    case Qt::Key_Left:
    case Qt::Key_Up:
        step(-1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        choose(frontIndex());
        break;
    case Qt::Key_Escape:
        choose(-1);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void Flip::keyReleaseEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        choose(frontIndex());
        break;
    default:
        QWidget::keyReleaseEvent(event);
    }
}

void Flip::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        choose(cardAt(event->position()));
}

void Flip::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta != 0)
        step(delta > 0 ? -1 : 1);
}

} // namespace argus
