#include "Overview.hpp"

#include "AppGrid.hpp"
#include "Background.hpp"
#include "Canvas.hpp"
#include <Layer.hpp>

#include <Icons.hpp>
#include <tde/Theme.hpp>

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <utility>

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

QRectF interpolate(const QRectF& from, const QRectF& to, double t)
{
    return QRectF(from.topLeft() + (to.topLeft() - from.topLeft()) * t, from.size() + (to.size() - from.size()) * t);
}

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

Overview::Overview(Toplevels& toplevels, const Wallpaper& wallpaper, QScreen* screen, bool primary, QWidget* parent)
    : QWidget(parent)
    , m_toplevels(toplevels)
    , m_wallpaper(wallpaper)
    , m_primary(primary)
{
    setWindowTitle(u"Overview"_s);
    setMouseTracking(true);
    // The windows show through while it opens and closes.
    setAttribute(Qt::WA_TranslucentBackground);
    m_canvas = new Canvas(this, [this](QPainter& painter) { paint(painter); });

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

    m_appsButton = new QToolButton(this);
    m_appsButton->setIconSize(QSize(24, 24));
    updateLook();
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
    connect(&m_toplevels.catalog(), &shell::Catalog::changed, this, [this] {
        if (showingApps())
            updateMode();
    });

    // Over everything; the primary screen's takes the keyboard while shown.
    shell::coverScreen(*this, LayerShellQt::Window::LayerOverlay, primary, u"tde-argus"_s, screen);
    setPrimary(primary);

    connect(&m_toplevels, &Toplevels::windowsChanged, this, [this] {
        if (isVisible())
            relayout();
    });
    connect(&m_toplevels, &Toplevels::previewChanged, this, [this] {
        if (isVisible())
            relayout();
    });

    for (QWidget* widget : std::initializer_list<QWidget*> {m_search, m_appsButton, m_grid}) {
        auto* fade = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(fade);
        m_fades.push_back(fade);
    }
    connect(&m_shown, &QVariantAnimation::valueChanged, this, [this] {
        for (auto* fade : m_fades)
            fade->setOpacity(m_shown.now());
        redraw();
    });
    connect(&m_shown, &QVariantAnimation::finished, this, [this] {
        if (m_closing) {
            m_closing = false;
            hide();
            setLive(false);
        }
    });
}

Overview::~Overview()
{
    // Its screen went while it was open: the pictures need not follow the windows for it.
    setLive(false);
}

void Overview::updateLook()
{
    const auto& colors = tde::theme::colors();
    m_appsButton->setIcon(
        shell::tintedIcon(u"view-app-grid-symbolic"_s, m_appsButton->iconSize(), devicePixelRatioF(), colors.text));
    // Round, highlighted under the pointer and while the applications are shown.
    m_appsButton->setStyleSheet(u"QToolButton { background: transparent; border: none; border-radius: %1px; }"
                                " QToolButton:hover { background: %2; }"
                                " QToolButton:pressed, QToolButton:checked { background: %3; }"_s.arg(ButtonSize / 2)
                                    .arg(colors.hover.name(QColor::HexArgb), colors.pressed.name(QColor::HexArgb)));
}

void Overview::changeEvent(QEvent* event)
{
    // The theme changed.
    if (event->type() == QEvent::PaletteChange)
        updateLook();
    QWidget::changeEvent(event);
}

void Overview::Show()
{
    // Closing, it turns around.
    if (m_closing) {
        m_closing = false;
        m_chosen = 0;
        animateTo(1);
        return;
    }
    if (isVisible() || m_opening)
        return;
    m_hovered = 0;
    m_hoveringClose = false;
    m_chosen = 0;
    m_search->clear();
    m_appsButton->setChecked(false);
    // It opens once the windows are pictured and placed, or a moment later without them.
    m_opening = true;
    m_toplevels.refresh(this, [this] { appear(); });
}

void Overview::appear()
{
    if (!m_opening)
        return;
    m_opening = false;
    relayout();
    m_shown.jump(0);
    for (auto* fade : m_fades)
        fade->setOpacity(0);
    show();
    m_search->setFocus();
    animateTo(1);
    // The windows go on showing what they do.
    setLive(true);
}

void Overview::setLive(bool live)
{
    if (live != m_live) {
        m_live = live;
        m_toplevels.setLive(live);
    }
}

void Overview::closeOnto(quint64 chosen)
{
    m_opening = false;
    if (!isVisible() || m_closing)
        return;
    m_closing = true;
    m_chosen = chosen;
    m_hovered = 0;
    animateTo(0);
}

void Overview::animateTo(double shown)
{
    m_shown.go(shown, int(m_animationTime * std::abs(shown - m_shown.now())));
}

void Overview::showApplications()
{
    Show();
    m_search->clear();
    m_appsButton->setChecked(true);
}

void Overview::setPrimary(bool primary)
{
    // Searching and the applications are there, with the keyboard.
    m_primary = primary;
    m_search->setVisible(primary);
    m_appsButton->setVisible(primary);
    if (auto* layer = LayerShellQt::Window::get(windowHandle())) {
        layer->setKeyboardInteractivity(primary ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                                : LayerShellQt::Window::KeyboardInteractivityNone);
    }
}

bool Overview::showsAllApplications() const
{
    return isOpen() && m_appsButton->isChecked() && m_search->text().isEmpty();
}

bool Overview::showsWindow(const Toplevel& window) const
{
    const QPoint centre = window.frame.center();
    if (window.frame.isEmpty() || !QGuiApplication::screenAt(centre))
        return m_primary;
    return screen() && screen()->geometry().contains(centre);
}

bool Overview::showingApps() const
{
    return !m_search->text().trimmed().isEmpty() || m_appsButton->isChecked();
}

void Overview::updateMode()
{
    if (!showingApps()) {
        m_grid->hide();
        redraw();
        return;
    }
    const QString query = m_search->text().trimmed();
    if (query.isEmpty()) {
        std::vector<const Application*> all;
        for (const Application& app : m_toplevels.catalog().applications()) {
            if (app.inMenus)
                all.push_back(&app);
        }
        m_grid->setApplications(std::move(all));
    } else {
        m_grid->setApplications(shell::search(m_toplevels.catalog().applications(), query));
    }
    m_hovered = 0;
    m_grid->show();
    redraw();
}

void Overview::launch(const Application* app)
{
    if (!shell::launch(*app))
        qWarning("tde-argus: could not start %s", qPrintable(app->id));
    emit closeRequested(0);
}

void Overview::relayout()
{
    m_search->move((width() - SearchWidth) / 2, Margin / 2);
    m_appsButton->move((width() - ButtonSize) / 2, height() - ButtonSize - Margin / 3);
    const QRect area = rect().adjusted(
        Margin, Margin / 2 + SearchHeight + Spacing, -Margin, -(ButtonSize + Margin / 3 + Spacing / 2));
    m_grid->setGeometry(area);

    std::vector<const Toplevel*> windows;
    for (const auto& window : m_toplevels.windows()) {
        if (showsWindow(*window))
            windows.push_back(window.get());
    }
    const qreal ratio = devicePixelRatioF();
    std::vector<QSize> sizes;
    sizes.reserve(windows.size());
    for (const Toplevel* window : windows) {
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
    redraw();
}

void Overview::redraw()
{
    m_canvas->update();
}

void Overview::paint(QPainter& painter)
{
    const auto& colors = tde::theme::colors();
    const double shown = m_shown.now();
    paintBackdrop(painter, *this, m_wallpaper, shown);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (showingApps())
        return;

    // Stacked as they are on the screen, the one used last on top, so nothing jumps where
    // they overlap as they leave or reach their places; the one it closes onto above them all.
    std::vector<std::pair<const Slot*, const Toplevel*>> order;
    for (const Slot& slot : m_slots) {
        if (const Toplevel* window = m_toplevels.find(slot.id))
            order.emplace_back(&slot, window);
    }
    std::ranges::stable_sort(order, [this](const auto& a, const auto& b) {
        const auto depth = [this](const Toplevel* window) {
            if (window->id == m_chosen)
                return -1;
            return window->depth();
        };
        return depth(a.second) > depth(b.second);
    });

    for (const auto& [slot, shownWindow] : order) {
        const Toplevel& window = *shownWindow;
        // Being dragged, it is drawn under the pointer instead.
        if (m_dragging && slot->id == m_pressed)
            continue;
        const bool hovered = slot->id == m_hovered && settled();

        // From where the window is to its place; those not on the screen grow in its place.
        QRectF start = placeOf(window, *this);
        double opacity = 1;
        if (start.isEmpty()) {
            const QRectF target(slot->preview);
            start = QRectF(QPointF(), target.size() * 0.85);
            start.moveCenter(target.center());
            opacity = shown;
        }
        const QRectF preview = interpolate(start, QRectF(slot->preview), shown);
        const double radius = Radius * shown;
        painter.setOpacity(opacity);

        if (hovered) {
            painter.setPen(QPen(colors.accent, 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(preview.adjusted(-4, -4, 4, 4), Radius + 3, Radius + 3);
        }

        QPainterPath shape;
        shape.addRoundedRect(preview, radius, radius);
        if (window.preview.isNull()) {
            paintPlaceholder(painter, shape, m_toplevels.iconOf(window));
        } else {
            painter.save();
            painter.setClipPath(shape);
            painter.drawImage(preview, window.preview);
            painter.restore();
        }

        painter.setOpacity(shown);
        painter.setPen(hovered ? colors.text : colors.dimText);
        painter.drawText(slot->title, Qt::AlignCenter,
            painter.fontMetrics().elidedText(window.displayName(), Qt::ElideRight, slot->title.width()));
        painter.setOpacity(1);

        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_hoveringClose ? colors.closeHover : colors.header);
            painter.drawEllipse(slot->closeButton);
            painter.setPen(QPen(m_hoveringClose ? Qt::white : colors.text, 1.5, Qt::SolidLine, Qt::RoundCap));
            const QPointF c = QRectF(slot->closeButton).center();
            constexpr qreal s = 4.0;
            painter.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
            painter.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
        }
    }

    // A window being dragged, or dragged here from another screen, under the pointer.
    const auto drawHeld = [&](quint64 id, QPoint at, double width) {
        const Toplevel* held = m_toplevels.find(id);
        if (!held)
            return;
        const QSizeF natural = held->preview.isNull() ? QSizeF(PlaceholderSize) : QSizeF(held->preview.size());
        const QSizeF size = natural.scaled(QSizeF(width, width), Qt::KeepAspectRatio);
        QRectF place(QPointF(), size);
        place.moveCenter(at);
        QPainterPath shape;
        shape.addRoundedRect(place, Radius, Radius);
        painter.setOpacity(0.85);
        if (held->preview.isNull()) {
            paintPlaceholder(painter, shape, m_toplevels.iconOf(*held));
        } else {
            painter.save();
            painter.setClipPath(shape);
            painter.drawImage(place, held->preview);
            painter.restore();
        }
        painter.setOpacity(1);
        painter.setPen(QPen(colors.accent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
    };
    if (m_dragging) {
        const auto slot = std::ranges::find(m_slots, m_pressed, &Slot::id);
        drawHeld(m_pressed, m_dragPos,
            slot == m_slots.end() ? 240.0 : double(std::max(slot->preview.width(), slot->preview.height())));
    }
    if (m_dropId != 0) {
        // The whole screen is where it goes.
        painter.setPen(QPen(colors.accent, 4));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), Radius, Radius);
        drawHeld(m_dropId, m_dropPos, 240);
    }
}

void Overview::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_canvas->setGeometry(rect());
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
        redraw();
    }
}

void Overview::mousePressEvent(QMouseEvent* event)
{
    m_pressed = 0;
    m_dragging = false;
    if (m_closing || event->button() != Qt::LeftButton || QGuiApplication::screens().size() < 2
        || !m_toplevels.canMoveWindows())
        return;
    // A window may be dragged to another screen.
    const QPoint pos = event->position().toPoint();
    if (const Slot* slot = slotAt(pos); slot && !slot->closeButton.contains(pos)) {
        m_pressed = slot->id;
        m_pressPos = pos;
    }
}

void Overview::showDrop(quint64 id, std::optional<QPoint> pos)
{
    const quint64 wanted = pos ? id : 0;
    if (wanted == m_dropId && (!pos || *pos == m_dropPos))
        return;
    m_dropId = wanted;
    m_dropPos = pos.value_or(QPoint());
    redraw();
}

void Overview::mouseMoveEvent(QMouseEvent* event)
{
    if (m_closing)
        return;
    if (m_pressed != 0) {
        const QPoint pos = event->position().toPoint();
        if (!m_dragging && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
            m_dragging = true;
        if (m_dragging) {
            m_dragPos = pos;
            redraw();
            emit windowDragged(m_pressed, screen()->geometry().topLeft() + pos);
            return;
        }
    }
    const Slot* slot = slotAt(event->position().toPoint());
    const bool onClose = slot && slot->closeButton.contains(event->position().toPoint());
    if (onClose != m_hoveringClose) {
        m_hoveringClose = onClose;
        redraw();
    }
    setHovered(slot ? slot->id : 0);
}

void Overview::leaveEvent(QEvent*)
{
    setHovered(0);
}

void Overview::mouseReleaseEvent(QMouseEvent* event)
{
    const quint64 pressed = std::exchange(m_pressed, 0);
    if (std::exchange(m_dragging, false)) {
        redraw();
        emit windowDropped(pressed, screen()->geometry().topLeft() + event->position().toPoint());
        return;
    }
    if (m_closing || (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton))
        return;
    const Slot* slot = slotAt(event->position().toPoint());
    if (!slot && showingApps()) {
        // Beside the applications: back to the windows.
        m_search->clear();
        m_appsButton->setChecked(false);
        return;
    }
    if (!slot) {
        emit closeRequested(0);
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
    if (m_closing)
        return true;
    if (event->key() == Qt::Key_Escape) {
        // Back one step: from a search, from all applications, then out of the overview.
        if (!m_search->text().isEmpty())
            m_search->clear();
        else if (m_appsButton->isChecked())
            m_appsButton->setChecked(false);
        else
            emit closeRequested(0);
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
    // The window comes forward behind the overview, which closes onto it.
    m_toplevels.activate(id);
    emit closeRequested(id);
}

} // namespace argus
