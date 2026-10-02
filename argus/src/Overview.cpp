#include "Overview.hpp"

#include "AppGrid.hpp"

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <LayerShellQt/Window>

#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

constexpr int Margin = 48;
constexpr int Spacing = 32;
constexpr int Radius = 8;
constexpr int CloseSize = 26;
constexpr int SearchWidth = 360;
constexpr int SearchHeight = 38;
constexpr int ButtonSize = 48;
const QSize PlaceholderSize(480, 320); // for windows not captured yet

} // namespace

std::vector<QRect> layOut(const std::vector<QSize>& sizes, const QRect& area, int spacing, int titleHeight)
{
    const int count = int(sizes.size());
    if (count == 0 || area.isEmpty())
        return {};

    struct Row {
        int first = 0;
        int count = 0;
        std::vector<QSizeF> scaled;
        double width = 0;
        double height = 0;
    };

    // Try every number of rows and keep the one that shows the windows largest.
    std::vector<Row> best;
    double bestArea = -1;
    for (int rows = 1; rows <= count; ++rows) {
        const int perRow = (count + rows - 1) / rows;
        const double rowHeight = double(area.height() - (rows - 1) * spacing) / rows - titleHeight;
        if (rowHeight <= 0)
            break;

        std::vector<Row> layout;
        double total = 0;
        for (int first = 0; first < count; first += perRow) {
            Row row;
            row.first = first;
            row.count = std::min(perRow, count - first);
            double naturalWidth = 0;
            for (int i = first; i < first + row.count; ++i) {
                const QSizeF size = sizes[size_t(i)];
                const double scale = std::min(1.0, rowHeight / size.height());
                row.scaled.push_back(size * scale);
                naturalWidth += size.width() * scale;
            }
            const double available = area.width() - (row.count - 1) * spacing;
            if (naturalWidth > available) {
                for (QSizeF& size : row.scaled)
                    size *= available / naturalWidth;
            }
            for (const QSizeF& size : row.scaled) {
                row.width += size.width();
                row.height = std::max(row.height, size.height());
                total += size.width() * size.height();
            }
            row.width += (row.count - 1) * spacing;
            layout.push_back(std::move(row));
        }
        if (total > bestArea) {
            bestArea = total;
            best = std::move(layout);
        }
    }

    double height = 0;
    for (const Row& row : best)
        height += row.height + titleHeight;
    height += (int(best.size()) - 1) * spacing;

    std::vector<QRect> rects(sizes.size());
    double y = area.top() + (area.height() - height) / 2;
    for (const Row& row : best) {
        double x = area.left() + (area.width() - row.width) / 2;
        for (int i = 0; i < row.count; ++i) {
            const QSizeF size = row.scaled[size_t(i)];
            const double top = y + (row.height - size.height()) / 2;
            rects[size_t(row.first + i)] = QRectF(QPointF(x, top), size).toAlignedRect();
            x += size.width() + spacing;
        }
        y += row.height + titleHeight + spacing;
    }
    return rects;
}

Overview::Overview(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(u"Overview"_s);
    setMouseTracking(true);

    // Typing anywhere goes to the search field, which hands the keys that move around on to us.
    m_search = new QLineEdit(this);
    m_search->setObjectName(u"search"_s);
    m_search->setPlaceholderText(u"Type to search"_s);
    m_search->setFixedSize(SearchWidth, SearchHeight);
    m_search->setStyleSheet(
        u"#search { border-radius: %1px; padding: 0 %2px; }"_s.arg(SearchHeight / 2).arg(SearchHeight / 3));
    m_search->installEventFilter(this);
    setFocusProxy(m_search);
    connect(m_search, &QLineEdit::textChanged, this, &Overview::updateMode);

    const auto& colors = tde::theme::colors();
    m_appsButton = new QToolButton(this);
    m_appsButton->setIconSize(QSize(24, 24));
    m_appsButton->setIcon(
        shell::tintedIcon(u"view-app-grid-symbolic"_s, m_appsButton->iconSize(), devicePixelRatioF(), colors.text));
    // Round, highlighted under the pointer and while the applications are shown.
    m_appsButton->setStyleSheet(u"QToolButton { background: transparent; border: none; border-radius: %1px; }"
                                " QToolButton:hover { background: %2; }"
                                " QToolButton:pressed, QToolButton:checked { background: %3; }"_s.arg(ButtonSize / 2)
                                    .arg(colors.hover.name(QColor::HexArgb), colors.pressed.name(QColor::HexArgb)));
    m_appsButton->setFixedSize(ButtonSize, ButtonSize);
    m_appsButton->setToolTip(u"Show Applications"_s);
    m_appsButton->setCheckable(true);
    m_appsButton->setAutoRaise(true);
    m_appsButton->setFocusPolicy(Qt::NoFocus);
    connect(m_appsButton, &QToolButton::toggled, this, &Overview::updateMode);

    m_grid = new AppGrid(this);
    m_grid->hide();
    connect(m_grid, &AppGrid::activated, this, &Overview::launch);

    // The grid points into the applications, which are new now.
    connect(&m_catalog, &shell::Catalog::changed, this, [this] {
        if (showingApps())
            updateMode();
    });

    // A layer over everything, covering the whole screen and taking the keyboard while shown.
    create();
    if (auto* layer = LayerShellQt::Window::get(windowHandle())) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layer->setScope(u"tde-argus"_s);
    }

    connect(&m_toplevels, &Toplevels::windowsChanged, this, [this] {
        if (isVisible())
            relayout();
    });
    connect(&m_toplevels, &Toplevels::previewChanged, this, [this] {
        if (isVisible())
            relayout();
    });
}

void Overview::Toggle()
{
    if (isVisible())
        Hide();
    else
        Show();
}

void Overview::Show()
{
    if (isVisible())
        return;
    m_hovered = 0;
    m_hoveringClose = false;
    m_search->clear();
    m_appsButton->setChecked(false);
    m_toplevels.capturePreviews();
    relayout();
    show();
    m_search->setFocus();
}

void Overview::Hide()
{
    hide();
}

void Overview::ToggleApplications()
{
    if (isVisible() && m_appsButton->isChecked() && m_search->text().isEmpty()) {
        Hide();
        return;
    }
    Show();
    m_search->clear();
    m_appsButton->setChecked(true);
}

bool Overview::showingApps() const
{
    return !m_search->text().trimmed().isEmpty() || m_appsButton->isChecked();
}

void Overview::updateMode()
{
    if (!showingApps()) {
        m_grid->hide();
        update();
        return;
    }
    const QString query = m_search->text().trimmed();
    if (query.isEmpty()) {
        std::vector<const Application*> all;
        for (const Application& app : m_catalog.applications()) {
            if (app.inMenus)
                all.push_back(&app);
        }
        m_grid->setApplications(std::move(all));
    } else {
        m_grid->setApplications(shell::search(m_catalog.applications(), query));
    }
    m_hovered = 0;
    m_grid->show();
    update();
}

void Overview::launch(const Application* app)
{
    if (!shell::launch(*app))
        qWarning("tde-argus: could not start %s", qPrintable(app->id));
    Hide();
}

void Overview::relayout()
{
    m_search->move((width() - SearchWidth) / 2, Margin / 2);
    m_appsButton->move((width() - ButtonSize) / 2, height() - ButtonSize - Margin / 3);
    const QRect area = rect().adjusted(
        Margin, Margin / 2 + SearchHeight + Spacing, -Margin, -(ButtonSize + Margin / 3 + Spacing / 2));
    m_grid->setGeometry(area);

    const auto& windows = m_toplevels.windows();
    const qreal ratio = devicePixelRatioF();
    std::vector<QSize> sizes;
    sizes.reserve(windows.size());
    for (const auto& window : windows) {
        const QSize size = window->preview.isNull() ? PlaceholderSize : (window->preview.size() / ratio);
        sizes.push_back(size.isEmpty() ? PlaceholderSize : size);
    }

    const int titleHeight = fontMetrics().height() + 12;
    const std::vector<QRect> rects = layOut(sizes, area, Spacing, titleHeight);

    m_slots.clear();
    for (size_t i = 0; i < rects.size(); ++i) {
        const QRect preview = rects[i];
        m_slots.push_back(Slot {
            .id = windows[i]->id,
            .preview = preview,
            .title = QRect(preview.left() - Spacing / 2, preview.bottom() + 1, preview.width() + Spacing, titleHeight),
            .closeButton = QRect(preview.right() - CloseSize / 2, preview.top() - CloseSize / 2, CloseSize, CloseSize),
        });
    }
    if (!std::ranges::any_of(m_slots, [this](const Slot& slot) { return slot.id == m_hovered; }))
        m_hovered = 0;
    update();
}

void Overview::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), colors.header.darker(140));
    if (showingApps())
        return;

    for (const Slot& slot : m_slots) {
        const auto it
            = std::ranges::find_if(m_toplevels.windows(), [&](const auto& window) { return window->id == slot.id; });
        if (it == m_toplevels.windows().end())
            continue;
        const Toplevel& window = **it;
        const bool hovered = slot.id == m_hovered;

        if (hovered) {
            painter.setPen(QPen(colors.accent, 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(slot.preview).adjusted(-4, -4, 4, 4), Radius + 3, Radius + 3);
        }

        QPainterPath shape;
        shape.addRoundedRect(QRectF(slot.preview), Radius, Radius);
        if (window.preview.isNull()) {
            painter.fillPath(shape, colors.window);
            const QIcon icon = shell::applicationIcon(nullptr, window.appId.toLower());
            const int size = std::min({96, slot.preview.width() / 2, slot.preview.height() / 2});
            icon.paint(&painter, QRect(slot.preview.center() - QPoint(size / 2, size / 2), QSize(size, size)));
        } else {
            painter.save();
            painter.setClipPath(shape);
            painter.drawImage(QRectF(slot.preview), window.preview);
            painter.restore();
        }

        const QString title = window.title.isEmpty() ? window.appId : window.title;
        painter.setPen(hovered ? colors.text : colors.dimText);
        painter.drawText(
            slot.title, Qt::AlignCenter, painter.fontMetrics().elidedText(title, Qt::ElideRight, slot.title.width()));

        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_hoveringClose ? colors.closeHover : colors.header);
            painter.drawEllipse(slot.closeButton);
            painter.setPen(QPen(m_hoveringClose ? Qt::white : colors.text, 1.5, Qt::SolidLine, Qt::RoundCap));
            const QPointF c = QRectF(slot.closeButton).center();
            constexpr qreal s = 4.0;
            painter.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
            painter.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
        }
    }
}

void Overview::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout();
}

const Slot* Overview::slotAt(QPoint pos) const
{
    if (showingApps())
        return nullptr;
    for (const Slot& slot : m_slots) {
        if (slot.preview.contains(pos) || slot.title.contains(pos)
            || (slot.id == m_hovered && slot.closeButton.contains(pos)))
            return &slot;
    }
    return nullptr;
}

void Overview::setHovered(quint64 id)
{
    if (id != m_hovered) {
        m_hovered = id;
        update();
    }
}

void Overview::mouseMoveEvent(QMouseEvent* event)
{
    const Slot* slot = slotAt(event->position().toPoint());
    const bool onClose = slot && slot->closeButton.contains(event->position().toPoint());
    if (onClose != m_hoveringClose) {
        m_hoveringClose = onClose;
        update();
    }
    setHovered(slot ? slot->id : 0);
}

void Overview::leaveEvent(QEvent*)
{
    setHovered(0);
}

void Overview::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton)
        return;
    const Slot* slot = slotAt(event->position().toPoint());
    if (!slot && showingApps()) {
        // Beside the applications: back to the windows.
        m_search->clear();
        m_appsButton->setChecked(false);
        return;
    }
    if (!slot) {
        Hide();
        return;
    }
    if (event->button() == Qt::MiddleButton || slot->closeButton.contains(event->position().toPoint()))
        m_toplevels.close(slot->id);
    else
        activate(slot->id);
}

bool Overview::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress)
        return handleKey(static_cast<QKeyEvent*>(event));
    return QWidget::eventFilter(watched, event);
}

// Keys that select and open things; everything else is typed into the search.
bool Overview::handleKey(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        // Back one step: from a search, from all applications, then out of the overview.
        if (!m_search->text().isEmpty())
            m_search->clear();
        else if (m_appsButton->isChecked())
            m_appsButton->setChecked(false);
        else
            Hide();
        return true;
    }

    if (showingApps()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            m_grid->activateSelected();
            return true;
        case Qt::Key_Right:
        case Qt::Key_Tab:
            m_grid->moveSelection(1, 0);
            return true;
        case Qt::Key_Left:
        case Qt::Key_Backtab:
            m_grid->moveSelection(-1, 0);
            return true;
        case Qt::Key_Down:
            m_grid->moveSelection(0, 1);
            return true;
        case Qt::Key_Up:
            m_grid->moveSelection(0, -1);
            return true;
        default:
            return false;
        }
    }

    const auto current = std::ranges::find_if(m_slots, [this](const Slot& slot) { return slot.id == m_hovered; });
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (current != m_slots.end())
            activate(current->id);
        else if (!m_slots.empty())
            activate(m_slots.front().id);
        return true;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Tab:
        if (!m_slots.empty())
            setHovered(
                current == m_slots.end() || current + 1 == m_slots.end() ? m_slots.front().id : (current + 1)->id);
        return true;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_Backtab:
        if (!m_slots.empty())
            setHovered(current == m_slots.end() || current == m_slots.begin() ? m_slots.back().id : (current - 1)->id);
        return true;
    case Qt::Key_Delete:
        if (current != m_slots.end())
            m_toplevels.close(current->id);
        return true;
    default:
        return false;
    }
}

void Overview::activate(quint64 id)
{
    Hide();
    m_toplevels.activate(id);
}

} // namespace argus
