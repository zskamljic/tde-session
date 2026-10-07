#include "Switcher.hpp"

#include <tde/Theme.hpp>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

constexpr int Padding = 16; // inside the panel
constexpr int Spacing = 8; // between the items
constexpr int Inset = 10; // inside an item, around what it shows
constexpr int Margin = 48; // the least room around the panel
constexpr int Radius = 12;
const QSize PreviewSize(200, 130);
constexpr int PreviewIcon = 40;
constexpr int IconSize = 72;

} // namespace

Switcher::Switcher(Toplevels& toplevels, QWidget* parent)
    : Picker(toplevels, u"tde-argus-switcher"_s, parent)
{
    setWindowTitle(u"Switch Windows"_s);
    connect(&m_toplevels, &Toplevels::previewChanged, this, [this](quint64 id) {
        m_pictures.erase(id);
        if (isVisible())
            update();
    });
}

void Switcher::setStyle(Style style)
{
    m_style = style;
    update();
}

Switcher::Layout Switcher::layOut() const
{
    // Items as large as they come, unless that is wider than the screen.
    const QSize content = m_style == Style::Previews ? PreviewSize : QSize(IconSize, IconSize);
    const int room = width() - 2 * Margin - 2 * Padding - (count() - 1) * Spacing;
    const double scale = std::min(1.0, double(room) / (count() * (content.width() + 2 * Inset)));
    const QSize item = (content + QSize(2 * Inset, 2 * Inset)) * scale;

    const int titleHeight = fontMetrics().height() + Padding / 2;
    const QSize panel(
        count() * item.width() + (count() - 1) * Spacing + 2 * Padding, item.height() + titleHeight + 2 * Padding);
    Layout layout;
    layout.panel = QRect(QPoint((width() - panel.width()) / 2, (height() - panel.height()) / 2), panel);
    for (int i = 0; i < count(); ++i) {
        layout.items.emplace_back(
            layout.panel.topLeft() + QPoint(Padding + i * (item.width() + Spacing), Padding), item);
    }
    layout.title = QRect(layout.panel.left() + Padding, layout.panel.top() + Padding + item.height() + Padding / 2,
        panel.width() - 2 * Padding, fontMetrics().height());
    return layout;
}

void Switcher::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const Layout layout = layOut();
    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.window);
    painter.drawRoundedRect(QRectF(layout.panel).adjusted(0.5, 0.5, -0.5, -0.5), Radius, Radius);

    for (int i = 0; i < count(); ++i) {
        const Toplevel* shown = window(i);
        if (!shown)
            continue;
        const QRect item = layout.items[size_t(i)];
        if (i == picked()) {
            painter.setPen(QPen(colors.accent, 2));
            painter.setBrush(colors.pressed);
            painter.drawRoundedRect(QRectF(item).adjusted(1, 1, -1, -1), Radius - 4, Radius - 4);
        }
        const QRect inside = item.adjusted(Inset, Inset, -Inset, -Inset);
        const QIcon icon = m_toplevels.iconOf(*shown);
        if (m_style == Style::Icons || shown->preview.isNull()) {
            const int size = std::min(inside.width(), inside.height());
            icon.paint(&painter, QRect(inside.center() - QPoint(size / 2, size / 2), QSize(size, size)));
            continue;
        }
        // The picture, as large as fits, with the application's icon on its lower edge.
        QRect picture(QPoint(),
            (QSizeF(shown->preview.size()) / devicePixelRatioF()).toSize().scaled(inside.size(), Qt::KeepAspectRatio));
        picture.moveCenter(inside.center());
        QPainterPath shape;
        shape.addRoundedRect(QRectF(picture), 4, 4);
        painter.save();
        painter.setClipPath(shape);
        // Scaled once, not with every Tab.
        QImage& small = m_pictures[shown->id];
        if (small.size() != picture.size() * devicePixelRatioF())
            small = shown->preview.scaled(
                picture.size() * devicePixelRatioF(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(picture, small);
        painter.restore();
        const int iconSize = std::min(PreviewIcon, inside.height() / 2);
        icon.paint(&painter,
            QRect(QPoint(inside.center().x() - iconSize / 2, inside.bottom() - iconSize + Inset / 2),
                QSize(iconSize, iconSize)));
    }

    // The name of the window picked, below it as far as the panel allows.
    if (const Toplevel* current = window(picked())) {
        const QString name
            = painter.fontMetrics().elidedText(current->displayName(), Qt::ElideMiddle, layout.title.width());
        QRect line = layout.title;
        line.setWidth(std::min(layout.title.width(), painter.fontMetrics().horizontalAdvance(name) + 2));
        line.moveCenter(QPoint(layout.items[size_t(picked())].center().x(), line.center().y()));
        line.moveLeft(std::clamp(line.left(), layout.title.left(), layout.title.right() - line.width()));
        painter.setPen(colors.text);
        painter.drawText(line, Qt::AlignCenter, name);
    }
}

void Switcher::closing()
{
    m_pictures.clear();
    finish();
}

void Switcher::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const Layout layout = layOut();
    const auto it = std::ranges::find_if(
        layout.items, [&](const QRect& item) { return item.contains(event->position().toPoint()); });
    choose(it == layout.items.end() ? -1 : int(it - layout.items.begin()));
}

} // namespace argus
