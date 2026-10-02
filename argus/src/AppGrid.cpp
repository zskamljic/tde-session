#include "AppGrid.hpp"

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QTextLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

constexpr int CellWidth = 144;
constexpr int CellHeight = 136;
constexpr int IconSize = 64;
constexpr int MaxColumns = 8;
constexpr int Padding = 8;

// `text` broken into at most two lines of `width`, the second elided.
QStringList twoLines(const QString& text, const QFont& font, int width)
{
    const QFontMetrics metrics(font);
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine first = layout.createLine();
    if (!first.isValid()) {
        layout.endLayout();
        return {text};
    }
    first.setLineWidth(width);
    layout.endLayout();
    const QString head = text.left(first.textLength()).trimmed();
    const QString rest = text.mid(first.textLength()).trimmed();
    if (rest.isEmpty() || metrics.horizontalAdvance(head) > width)
        return {metrics.elidedText(text, Qt::ElideRight, width)};
    return {head, metrics.elidedText(rest, Qt::ElideRight, width)};
}

} // namespace

AppGrid::AppGrid(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
}

void AppGrid::setApplications(std::vector<const Application*> apps)
{
    m_apps = std::move(apps);
    m_selected = m_apps.empty() ? -1 : 0;
    m_hovered = -1;
    m_firstRow = 0;
    m_wheel = 0;
    update();
}

int AppGrid::columns() const
{
    return std::clamp(width() / CellWidth, 1, MaxColumns);
}

int AppGrid::rowCount() const
{
    return (int(m_apps.size()) + columns() - 1) / columns();
}

int AppGrid::visibleRows() const
{
    return std::max(1, height() / CellHeight);
}

QRect AppGrid::cellRect(int index) const
{
    const int perRow = columns();
    const int row = index / perRow - m_firstRow;
    const int column = index % perRow;
    // A row that is not full is centred like the full ones.
    const int inRow = std::min(perRow, int(m_apps.size()) - (index / perRow) * perRow);
    const int left = (width() - inRow * CellWidth) / 2;
    return {left + column * CellWidth, row * CellHeight, CellWidth, CellHeight};
}

int AppGrid::indexAt(QPoint pos) const
{
    const int row = pos.y() / CellHeight + m_firstRow;
    if (pos.y() < 0 || row >= rowCount())
        return -1;
    const int perRow = columns();
    for (int index = row * perRow; index < std::min(int(m_apps.size()), (row + 1) * perRow); ++index) {
        if (cellRect(index).contains(pos))
            return index;
    }
    return -1;
}

void AppGrid::scrollTo(int row)
{
    row = std::clamp(row, 0, std::max(0, rowCount() - visibleRows()));
    if (row != m_firstRow) {
        m_firstRow = row;
        m_hovered = -1;
        update();
    }
}

void AppGrid::ensureVisible(int index)
{
    const int row = index / columns();
    if (row < m_firstRow)
        scrollTo(row);
    else if (row >= m_firstRow + visibleRows())
        scrollTo(row - visibleRows() + 1);
}

void AppGrid::moveSelection(int columnsBy, int rowsBy)
{
    const int count = int(m_apps.size());
    if (count == 0)
        return;
    int index = std::max(m_selected, 0);
    if (rowsBy != 0) {
        const int target = index + rowsBy * columns();
        // Down from the row above a shorter last one lands on its last app; past the top or
        // bottom row the selection stays.
        if (target >= 0 && target < count)
            index = target;
        else if (target >= count && index / columns() < rowCount() - 1)
            index = count - 1;
    }
    index = ((index + columnsBy) % count + count) % count;
    m_selected = index;
    ensureVisible(index);
    update();
}

void AppGrid::activateSelected()
{
    if (m_selected >= 0 && m_selected < int(m_apps.size()))
        emit activated(m_apps[size_t(m_selected)]);
}

void AppGrid::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_apps.empty()) {
        painter.setPen(colors.dimText);
        painter.drawText(rect().adjusted(0, 0, 0, -height() / 2), Qt::AlignCenter, u"No applications found"_s);
        return;
    }

    const int perRow = columns();
    const int first = m_firstRow * perRow;
    const int last = std::min(int(m_apps.size()), (m_firstRow + visibleRows()) * perRow);
    const int radius = tde::theme::radius(tde::theme::RadiusSize::Large);
    const QFontMetrics metrics = fontMetrics();
    for (int index = first; index < last; ++index) {
        const Application& app = *m_apps[size_t(index)];
        const QRect cell = cellRect(index).adjusted(Padding / 2, Padding / 2, -Padding / 2, -Padding / 2);
        if (index == m_selected || index == m_hovered) {
            QColor highlight = index == m_selected ? colors.accent : colors.hover;
            highlight.setAlphaF(index == m_selected ? 0.35 : 0.6);
            painter.setPen(Qt::NoPen);
            painter.setBrush(highlight);
            painter.drawRoundedRect(cell, radius, radius);
        }

        const QRect icon(cell.left() + (cell.width() - IconSize) / 2, cell.top() + Padding, IconSize, IconSize);
        shell::applicationIcon(&app).paint(&painter, icon);

        painter.setPen(colors.text);
        const QStringList lines = twoLines(app.name, font(), cell.width() - 2 * Padding);
        int y = icon.bottom() + Padding;
        for (const QString& line : lines) {
            painter.drawText(
                QRect(cell.left(), y, cell.width(), metrics.height()), Qt::AlignHCenter | Qt::AlignTop, line);
            y += metrics.height();
        }
    }

    // Where in the list the visible rows are, when not all of them fit.
    if (rowCount() > visibleRows()) {
        const double shown = double(visibleRows()) / rowCount();
        const double start = double(m_firstRow) / rowCount();
        const QRectF track(width() - 6, 0, 4, height());
        QColor thumb = colors.text;
        thumb.setAlphaF(0.4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(thumb);
        painter.drawRoundedRect(
            QRectF(track.left(), track.top() + start * track.height(), track.width(), shown * track.height()), 2, 2);
    }
}

void AppGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    scrollTo(m_firstRow);
    if (m_selected >= 0)
        ensureVisible(m_selected);
}

void AppGrid::mouseMoveEvent(QMouseEvent* event)
{
    const int index = indexAt(event->position().toPoint());
    if (index != m_hovered) {
        m_hovered = index;
        update();
    }
}

void AppGrid::leaveEvent(QEvent*)
{
    if (m_hovered >= 0) {
        m_hovered = -1;
        update();
    }
}

void AppGrid::mouseReleaseEvent(QMouseEvent* event)
{
    const int index = event->button() == Qt::LeftButton ? indexAt(event->position().toPoint()) : -1;
    if (index < 0) {
        event->ignore(); // the overview handles clicks beside the apps
        return;
    }
    emit activated(m_apps[size_t(index)]);
}

void AppGrid::wheelEvent(QWheelEvent* event)
{
    // A notch of the wheel (120) scrolls a row; touchpads add up smaller steps.
    m_wheel -= event->angleDelta().y() / 120.0;
    const int rows = int(m_wheel);
    m_wheel -= rows;
    if (rows != 0)
        scrollTo(m_firstRow + rows);
}

} // namespace argus
