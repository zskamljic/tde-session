#include "Screenshot.hpp"

#include <Layer.hpp>

#include <tde/Theme.hpp>

#include <QAbstractButton>
#include <QBoxLayout>
#include <QBuffer>
#include <QButtonGroup>
#include <QClipboard>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

const QString Notifications = u"org.freedesktop.Notifications"_s;
const QString NotificationsPath = u"/org/freedesktop/Notifications"_s;

constexpr int Grab = 8; // how near an edge of the selection grabs it
constexpr int SmallestSelection = 4;

// The round button that takes the screenshot: a ring around a disc.
class TakeButton : public QAbstractButton {
public:
    explicit TakeButton(QWidget* parent)
        : QAbstractButton(parent)
    {
        setFixedSize(52, 52);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip(u"Take Screenshot"_s);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& colors = tde::theme::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF ring = QRectF(rect()).adjusted(2, 2, -2, -2);
        painter.setPen(QPen(colors.text, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ring);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isDown() ? colors.dimText : underMouse() ? colors.text.lighter(110) : colors.text);
        painter.drawEllipse(ring.adjusted(6, 6, -6, -6));
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
};

} // namespace

// What one screen shows while a screenshot is taken: what it showed as it froze, dimmed but
// for what is picked, and on the primary screen the buttons.
class ShotLayer : public QWidget {
public:
    ShotLayer(Screenshot& owner, QScreen* screen, bool primary)
        : m_owner(owner)
        , m_screen(screen)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        shell::coverScreen(*this, LayerShellQt::Window::LayerOverlay, primary, u"tde-argus-screenshot"_s, screen);
        if (primary && !m_owner.isPickingColor())
            makePanel();
        updateCursor({});
    }

    QScreen* shownOn() const { return m_screen; }

    // The mode changed: the buttons follow.
    void sync()
    {
        if (QAbstractButton* button = m_modes ? m_modes->button(int(m_owner.mode())) : nullptr)
            button->setChecked(true);
        updateCursor(mapFromGlobal(QCursor::pos()));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const QImage image = m_owner.frozen(m_screen);
        if (image.isNull())
            painter.fillRect(rect(), Qt::black);
        else
            painter.drawImage(rect(), image);
        // A colour is picked from the screen as it is.
        if (m_owner.isPickingColor())
            return;

        // What is picked stays bright.
        QRect picked;
        switch (m_owner.mode()) {
        case Screenshot::Mode::Selection:
            if (m_owner.selectionScreen() == m_screen)
                picked = m_owner.selection();
            break;
        case Screenshot::Mode::Screen:
            if (m_owner.chosenScreen() == m_screen)
                picked = rect();
            break;
        case Screenshot::Mode::Window:
            if (const Toplevel* window = m_owner.toplevels().find(m_owner.chosenWindow()))
                picked = placeOf(*window, *this) & rect();
            break;
        }
        painter.setClipRegion(QRegion(rect()).subtracted(picked));
        painter.fillRect(rect(), QColor(0, 0, 0, 120));
        painter.setClipping(false);
        if (picked.isEmpty())
            return;

        painter.setRenderHint(QPainter::Antialiasing);
        const auto& colors = tde::theme::colors();
        if (m_owner.mode() == Screenshot::Mode::Selection) {
            painter.setPen(QPen(Qt::white, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(picked).adjusted(-1, -1, 1, 1));
            // Corners to pull on.
            painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
            painter.setBrush(Qt::white);
            for (const QPoint corner : {picked.topLeft(), picked.topRight() + QPoint(1, 0),
                     picked.bottomLeft() + QPoint(0, 1), picked.bottomRight() + QPoint(1, 1)})
                painter.drawEllipse(QPointF(corner), 6, 6);
        } else {
            painter.setPen(QPen(colors.accent, 4));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(picked).adjusted(2, 2, -2, -2));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const QPoint pos = event->position().toPoint();
        if (m_owner.isPickingColor()) {
            m_owner.pickColorAt(m_screen, pos);
            return;
        }
        switch (m_owner.mode()) {
        case Screenshot::Mode::Selection:
            m_pressed = pos;
            m_before = {m_owner.selectionScreen(), m_owner.selection()};
            m_edges = edgesAt(pos);
            if (m_edges == None)
                m_owner.select(m_screen, QRect(pos, QSize(0, 0)));
            break;
        case Screenshot::Mode::Screen:
            m_owner.chooseScreen(m_screen);
            break;
        case Screenshot::Mode::Window:
            if (const Toplevel* window = m_owner.windowAt(*this, pos))
                m_owner.chooseWindow(window->id);
            break;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint pos = event->position().toPoint();
        if (!(event->buttons() & Qt::LeftButton) || m_owner.mode() != Screenshot::Mode::Selection
            || m_owner.isPickingColor()) {
            updateCursor(pos);
            return;
        }
        const QPoint by = pos - m_pressed;
        QRect area = m_before.second;
        if (m_edges == None) {
            // Drawn anew, from where it was pressed: as wide as it was dragged.
            area = QRect(QPoint(std::min(m_pressed.x(), pos.x()), std::min(m_pressed.y(), pos.y())),
                QSize(std::abs(pos.x() - m_pressed.x()), std::abs(pos.y() - m_pressed.y())));
        } else if (m_edges == Inside) {
            area.translate(by);
            area.moveLeft(std::clamp(area.left(), 0, width() - area.width()));
            area.moveTop(std::clamp(area.top(), 0, height() - area.height()));
        } else {
            if (m_edges & Left)
                area.setLeft(std::min(area.left() + by.x(), area.right() - SmallestSelection));
            if (m_edges & Right)
                area.setRight(std::max(area.right() + by.x(), area.left() + SmallestSelection));
            if (m_edges & Top)
                area.setTop(std::min(area.top() + by.y(), area.bottom() - SmallestSelection));
            if (m_edges & Bottom)
                area.setBottom(std::max(area.bottom() + by.y(), area.top() + SmallestSelection));
        }
        m_owner.select(m_screen, area & rect());
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || m_owner.mode() != Screenshot::Mode::Selection
            || m_owner.isPickingColor())
            return;
        // A click without drawing keeps what was selected.
        const QRect area = m_owner.selection();
        if (m_edges == None && (area.width() < SmallestSelection || area.height() < SmallestSelection))
            m_owner.select(m_before.first, m_before.second);
        m_edges = None;
        updateCursor(event->position().toPoint());
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_owner.mode() != Screenshot::Mode::Selection
            && !m_owner.isPickingColor())
            m_owner.take();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (m_owner.isPickingColor() && event->key() != Qt::Key_Escape) {
            QWidget::keyPressEvent(event);
            return;
        }
        switch (event->key()) {
        case Qt::Key_Escape:
            m_owner.cancel();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            m_owner.take();
            break;
        case Qt::Key_S:
            m_owner.setMode(Screenshot::Mode::Selection);
            break;
        case Qt::Key_C:
            m_owner.setMode(Screenshot::Mode::Screen);
            break;
        case Qt::Key_W:
            m_owner.setMode(Screenshot::Mode::Window);
            break;
        default:
            QWidget::keyPressEvent(event);
        }
    }

private:
    enum Edge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8, Inside = 16 };

    // The edges of the selection at `pos`, Inside within it, or None.
    int edgesAt(QPoint pos) const
    {
        const QRect area = m_owner.selection();
        if (m_owner.selectionScreen() != m_screen || area.isEmpty()
            || !area.adjusted(-Grab, -Grab, Grab, Grab).contains(pos))
            return None;
        int edges = None;
        if (std::abs(pos.x() - area.left()) <= Grab)
            edges |= Left;
        else if (std::abs(pos.x() - area.right()) <= Grab)
            edges |= Right;
        if (std::abs(pos.y() - area.top()) <= Grab)
            edges |= Top;
        else if (std::abs(pos.y() - area.bottom()) <= Grab)
            edges |= Bottom;
        return edges == None && area.contains(pos) ? Inside : edges;
    }

    void updateCursor(QPoint pos)
    {
        if (m_owner.isPickingColor()) {
            setCursor(Qt::CrossCursor);
            return;
        }
        if (m_owner.mode() != Screenshot::Mode::Selection) {
            setCursor(Qt::PointingHandCursor);
            return;
        }
        switch (m_edges != None ? m_edges : edgesAt(pos)) {
        case Left | Top:
        case Right | Bottom:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Left | Bottom:
        case Right | Top:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Left:
        case Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case Top:
        case Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case Inside:
            setCursor(Qt::SizeAllCursor);
            break;
        default:
            setCursor(Qt::CrossCursor);
        }
    }

    // The modes, the button that takes it and the one that closes, at the bottom.
    void makePanel()
    {
        const auto& colors = tde::theme::colors();
        auto* panel = new QFrame(this);
        panel->setObjectName(u"ScreenshotPanel"_s);
        panel->setCursor(Qt::ArrowCursor);
        panel->setStyleSheet(
            u"#ScreenshotPanel { background: %1; border-radius: 18px; }"
            "QToolButton { color: %2; background: transparent; border: none; border-radius: 10px;"
            " padding: 8px 14px; }"
            "QToolButton:hover { background: %3; }"
            "QToolButton:checked { background: %4; color: %5; }"_s.arg(colors.window.name(), colors.text.name(),
                colors.hover.name(), colors.accent.name(), colors.accentText.name()));
        auto* row = new QHBoxLayout(panel);
        row->setContentsMargins(12, 10, 12, 10);
        row->setSpacing(6);

        m_modes = new QButtonGroup(panel);
        const std::pair<Screenshot::Mode, QString> modes[] = {
            {Screenshot::Mode::Selection, u"Selection"_s},
            {Screenshot::Mode::Screen, u"Screen"_s},
            {Screenshot::Mode::Window, u"Window"_s},
        };
        for (const auto& [mode, label] : modes) {
            auto* button = new QToolButton(panel);
            button->setText(label);
            button->setCheckable(true);
            button->setFocusPolicy(Qt::NoFocus);
            m_modes->addButton(button, int(mode));
            row->addWidget(button);
        }
        connect(m_modes, &QButtonGroup::idClicked, this,
            [this](int id) { m_owner.setMode(static_cast<Screenshot::Mode>(id)); });
        row->addSpacing(18);
        auto* take = new TakeButton(panel);
        connect(take, &QAbstractButton::clicked, this, [this] { m_owner.take(); });
        row->addWidget(take);
        row->addSpacing(18);
        auto* close = new QToolButton(panel);
        close->setText(u"✕"_s);
        close->setToolTip(u"Close"_s);
        close->setFocusPolicy(Qt::NoFocus);
        connect(close, &QAbstractButton::clicked, this, [this] { m_owner.cancel(); });
        row->addWidget(close);

        panel->adjustSize();
        const QSize size = panel->sizeHint();
        const QRect screen = m_screen ? m_screen->geometry() : QRect();
        panel->setGeometry(
            (screen.width() - size.width()) / 2, screen.height() - size.height() - 48, size.width(), size.height());
        sync();
    }

    Screenshot& m_owner;
    QScreen* m_screen;
    QButtonGroup* m_modes = nullptr; // on the primary screen
    QPoint m_pressed;
    int m_edges = None; // those being pulled on
    std::pair<QPointer<QScreen>, QRect> m_before; // the selection as the press found it
};

// Screenshot ------------------------------------------------------------------------------

Screenshot::Screenshot(Toplevels& toplevels, QObject* parent)
    : QObject(parent)
    , m_toplevels(toplevels)
{
    auto bus = QDBusConnection::sessionBus();
    bus.connect(Notifications, NotificationsPath, Notifications, u"ActionInvoked"_s, this,
        SLOT(notificationAction(uint, QString)));
    bus.connect(Notifications, NotificationsPath, Notifications, u"NotificationClosed"_s, this,
        SLOT(notificationClosed(uint, uint)));
}

Screenshot::~Screenshot() = default;

void Screenshot::Show()
{
    // Print again while shown takes it.
    if (!m_layers.empty() && !isPickingColor()) {
        take();
        return;
    }
    showFor({});
}

void Screenshot::showFor(Taken taken)
{
    if (m_busy) {
        if (taken)
            taken({});
        return;
    }
    m_taken = std::move(taken);
    freeze([this] {
        QScreen* primary = m_primary ? m_primary.data() : QGuiApplication::primaryScreen();
        // What was picked last, while it is still there.
        if (!m_chosenScreen)
            m_chosenScreen = primary;
        const Toplevel* chosen = m_toplevels.find(m_chosenWindow);
        if (!chosen || chosen->minimized || chosen->frame.isEmpty()) {
            m_chosenWindow = 0;
            for (const auto& window : m_toplevels.windows()) {
                if (!window->minimized && !window->frame.isEmpty()
                    && (!m_chosenWindow || window->depth() < m_toplevels.find(m_chosenWindow)->depth()))
                    m_chosenWindow = window->id;
            }
        }
        if (!m_selectionScreen || !QRect(QPoint(), m_selectionScreen->size()).contains(m_selection)) {
            m_selectionScreen = primary;
            const QSize size = m_selectionScreen ? m_selectionScreen->size() : QSize();
            m_selection = QRect(size.width() / 4, size.height() / 4, size.width() / 2, size.height() / 2);
        }
        for (QScreen* screen : QGuiApplication::screens()) {
            auto& layer = m_layers.emplace_back(std::make_unique<ShotLayer>(*this, screen, screen == primary));
            layer->show();
        }
        // The keyboard goes to the buttons' screen.
        for (const auto& layer : m_layers) {
            if (layer->shownOn() == primary) {
                layer->activateWindow();
                layer->setFocus();
            }
        }
    });
}

void Screenshot::TakeScreen()
{
    takeScreensFor({});
}

void Screenshot::takeScreensFor(Taken taken)
{
    if (m_busy) {
        if (taken)
            taken({});
        return;
    }
    freeze([this, taken = std::move(taken)] {
        const QImage image = allScreens();
        close();
        if (taken)
            taken(image);
        else if (!image.isNull())
            keep(image);
    });
}

void Screenshot::pickColor(ColorPicked picked)
{
    if (m_busy) {
        picked(std::nullopt);
        return;
    }
    m_colorPicked = std::move(picked);
    // The same frozen screens, without the buttons.
    showFor({});
}

void Screenshot::pickColorAt(QScreen* screen, QPoint point)
{
    const QImage image = frozen(screen);
    std::optional<QColor> color;
    if (!image.isNull() && screen) {
        const double scale = image.width() / double(std::max(1, screen->size().width()));
        const QPoint pixel(std::clamp(int(point.x() * scale), 0, image.width() - 1),
            std::clamp(int(point.y() * scale), 0, image.height() - 1));
        color = image.pixelColor(pixel);
    }
    const ColorPicked picked = std::exchange(m_colorPicked, {});
    close();
    if (picked)
        picked(color);
}

void Screenshot::TakeWindow()
{
    if (m_busy)
        return;
    freeze([this] {
        // The window used last, as it shows on its own.
        const Toplevel* active = nullptr;
        for (const auto& window : m_toplevels.windows()) {
            if (!window->minimized && !window->preview.isNull() && (!active || window->depth() < active->depth()))
                active = window.get();
        }
        const QImage image = active ? active->preview : QImage();
        close();
        if (!image.isNull())
            keep(image);
    });
}

void Screenshot::freeze(std::function<void()> then)
{
    m_busy = true;
    m_then = std::move(then);
    m_frozen.clear();
    m_copies.clear();
    for (QScreen* screen : QGuiApplication::screens()) {
        auto copy = m_capture.capture(screen, [this, screen](QImage image) {
            m_frozen[screen] = std::move(image);
            m_copies.erase(screen);
            frozenPart();
        });
        if (copy)
            m_copies[screen] = std::move(copy);
    }
    m_locatingWindows = true;
    m_toplevels.refresh(this, [this] {
        m_locatingWindows = false;
        frozenPart();
    });
}

void Screenshot::frozenPart()
{
    if (m_copies.empty() && !m_locatingWindows && m_then)
        std::exchange(m_then, {})();
}

void Screenshot::close()
{
    // Closing comes from the layers' own events, so they go once those are done.
    for (auto& layer : m_layers) {
        layer->hide();
        layer.release()->deleteLater();
    }
    m_layers.clear();
    m_frozen.clear();
    m_busy = false;
}

void Screenshot::cancel()
{
    const Taken taken = std::exchange(m_taken, {});
    const ColorPicked picked = std::exchange(m_colorPicked, {});
    close();
    if (taken)
        taken({});
    if (picked)
        picked(std::nullopt);
}

QImage Screenshot::frozen(QScreen* screen) const
{
    const auto it = m_frozen.find(screen);
    return it == m_frozen.end() ? QImage() : it->second;
}

void Screenshot::setMode(Mode mode)
{
    m_mode = mode;
    updateLayers();
}

void Screenshot::select(QScreen* screen, const QRect& area)
{
    m_selectionScreen = screen;
    m_selection = area;
    updateLayers();
}

void Screenshot::chooseScreen(QScreen* screen)
{
    m_chosenScreen = screen;
    updateLayers();
}

void Screenshot::chooseWindow(quint64 id)
{
    m_chosenWindow = id;
    updateLayers();
}

const Toplevel* Screenshot::windowAt(const QWidget& layer, QPoint point) const
{
    const Toplevel* found = nullptr;
    for (const auto& window : m_toplevels.windows()) {
        if (placeOf(*window, layer).contains(point) && (!found || window->depth() < found->depth()))
            found = window.get();
    }
    return found;
}

void Screenshot::updateLayers()
{
    for (const auto& layer : m_layers)
        layer->sync();
}

void Screenshot::take()
{
    if (isPickingColor())
        return;
    QImage image;
    switch (m_mode) {
    case Mode::Selection:
        if (m_selectionScreen && !m_selection.isEmpty()) {
            const QImage screen = frozen(m_selectionScreen);
            // From the screen's coordinates to its pixels.
            const double scale = screen.width() / double(std::max(1, m_selectionScreen->size().width()));
            const QRect pixels(int(std::round(m_selection.x() * scale)), int(std::round(m_selection.y() * scale)),
                int(std::round(m_selection.width() * scale)), int(std::round(m_selection.height() * scale)));
            image = screen.copy(pixels);
        }
        break;
    case Mode::Screen:
        image = frozen(m_chosenScreen);
        break;
    case Mode::Window:
        if (const Toplevel* window = m_toplevels.find(m_chosenWindow))
            image = window->preview;
        break;
    }
    if (image.isNull())
        return;
    const Taken taken = std::exchange(m_taken, {});
    close();
    if (taken)
        taken(image);
    else
        keep(image);
}

QImage Screenshot::allScreens() const
{
    const auto screens = QGuiApplication::screens();
    if (screens.size() == 1)
        return frozen(screens.front());
    QRect all;
    double scale = 1;
    for (QScreen* screen : screens) {
        all |= screen->geometry();
        const QImage image = frozen(screen);
        if (!image.isNull())
            scale = std::max(scale, image.width() / double(std::max(1, screen->geometry().width())));
    }
    QImage image((QSizeF(all.size()) * scale).toSize(), QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (QScreen* screen : screens) {
        const QRect place = screen->geometry().translated(-all.topLeft());
        painter.drawImage(QRectF(QPointF(place.topLeft()) * scale, QSizeF(place.size()) * scale), frozen(screen));
    }
    return image;
}

void Screenshot::keep(const QImage& image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    if (m_clipboard.isSupported())
        m_clipboard.set(u"image/png"_s, png);
    else
        QGuiApplication::clipboard()->setImage(image);

    notify(save(png));
}

QString Screenshot::save(const QByteArray& png)
{
    const QDir folder(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + u"/Screenshots"_s);
    folder.mkpath(u"."_s);
    const QString name = u"Screenshot From %1"_s.arg(QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH-mm-ss"_s));
    QString path = folder.filePath(name + u".png"_s);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = folder.filePath(u"%1 (%2).png"_s.arg(name).arg(n));
    QFile file(path);
    if (png.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(png) != png.size()) {
        qWarning("tde-argus: the screenshot could not be saved to %s", qPrintable(path));
        return {};
    }
    return path;
}

void Screenshot::notify(const QString& path)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Notifications, NotificationsPath, Notifications, u"Notify"_s);
    QVariantMap hints;
    QStringList actions;
    QString body = u"You can paste the image from the clipboard."_s;
    if (!path.isEmpty()) {
        hints.insert(u"image-path"_s, QUrl::fromLocalFile(path).toString());
        actions = {u"default"_s, u"Show in Files"_s};
        body = u"Saved in %1. You can paste the image from the clipboard."_s.arg(QFileInfo(path).dir().dirName());
    }
    message << u"Screenshot"_s << uint(0) << u"camera-photo"_s << u"Screenshot captured"_s << body << actions << hints
            << int(-1);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, path] {
        watcher->deleteLater();
        const QDBusPendingReply<uint> reply = *watcher;
        if (!reply.isError() && !path.isEmpty())
            m_notified[reply.value()] = path;
    });
}

void Screenshot::notificationAction(uint id, const QString& action)
{
    const auto it = m_notified.find(id);
    if (it == m_notified.end() || action != u"default")
        return;
    const QString path = it->second;
    // Shown in the file manager, or opened when there is none to show it.
    QDBusMessage message = QDBusMessage::createMethodCall(u"org.freedesktop.FileManager1"_s,
        u"/org/freedesktop/FileManager1"_s, u"org.freedesktop.FileManager1"_s, u"ShowItems"_s);
    message << QStringList {QUrl::fromLocalFile(path).toString()} << QString();
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, path] {
        watcher->deleteLater();
        if (watcher->isError())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
}

void Screenshot::notificationClosed(uint id, uint)
{
    m_notified.erase(id);
}

} // namespace argus
