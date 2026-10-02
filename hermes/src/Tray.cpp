#include "Tray.hpp"

#include "StatusNotifier.hpp"

#include <tde/Theme.hpp>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextDocument>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int CellWidth = 30;
constexpr int IconSize = 18;

} // namespace

Tray::Tray(QWidget* parent)
    : QWidget(parent)
    , m_watcher(std::make_unique<Watcher>())
{
    setMouseTracking(true);
    registerStatusNotifierTypes();

    auto bus = QDBusConnection::sessionBus();
    if (m_watcher->start()) {
        connect(m_watcher.get(), &Watcher::StatusNotifierItemRegistered, this, &Tray::add);
        connect(m_watcher.get(), &Watcher::StatusNotifierItemUnregistered, this, &Tray::remove);
    } else {
        // Another desktop's watcher runs already: be a host of it.
        m_watcher.reset();
        bus.connect(
            WatcherService, WatcherPath, WatcherService, u"StatusNotifierItemRegistered"_s, this, SLOT(add(QString)));
        bus.connect(WatcherService, WatcherPath, WatcherService, u"StatusNotifierItemUnregistered"_s, this,
            SLOT(remove(QString)));
        auto get = QDBusMessage::createMethodCall(
            WatcherService, WatcherPath, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        get << WatcherService << u"RegisteredStatusNotifierItems"_s;
        const QDBusMessage reply = bus.call(get);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            for (const QString& item : reply.arguments().first().value<QDBusVariant>().variant().toStringList())
                add(item);
        }
    }

    const QString host = u"org.kde.StatusNotifierHost-%1"_s.arg(QApplication::applicationPid());
    bus.registerService(host);
    auto registerHost
        = QDBusMessage::createMethodCall(WatcherService, WatcherPath, WatcherService, u"RegisterStatusNotifierHost"_s);
    registerHost << host;
    bus.call(registerHost, QDBus::NoBlock);
}

Tray::~Tray() = default;

void Tray::add(const QString& item)
{
    const qsizetype slash = item.indexOf(u'/');
    const QString service = slash < 0 ? item : item.left(slash);
    const QString path = slash < 0 ? u"/StatusNotifierItem"_s : item.mid(slash);
    if (std::ranges::any_of(m_items, [&](const auto& i) { return i->key() == service + path; }))
        return;
    auto notifier = std::make_unique<StatusNotifierItem>(service, path);
    connect(notifier.get(), &StatusNotifierItem::changed, this, &Tray::itemsChanged);
    m_items.push_back(std::move(notifier));
}

void Tray::remove(const QString& item)
{
    const QString key = item.contains(u'/') ? item : item + u"/StatusNotifierItem"_s;
    if (m_hovered && m_hovered->key() == key)
        m_hovered = nullptr;
    std::erase_if(m_items, [&](const auto& i) { return i->key() == key; });
    itemsChanged();
}

void Tray::itemsChanged()
{
    updateGeometry();
    update();
}

std::vector<StatusNotifierItem*> Tray::shown() const
{
    std::vector<StatusNotifierItem*> result;
    for (const auto& item : m_items) {
        if (!item->isPassive() && !item->icon().isNull())
            result.push_back(item.get());
    }
    return result;
}

QSize Tray::sizeHint() const
{
    return {int(shown().size()) * CellWidth, IconSize};
}

QRect Tray::iconRect(int index) const
{
    return {index * CellWidth, 0, CellWidth, height()};
}

StatusNotifierItem* Tray::itemAt(QPoint pos) const
{
    const auto items = shown();
    const int index = pos.x() / CellWidth;
    return pos.x() >= 0 && index < int(items.size()) && rect().contains(pos) ? items[size_t(index)] : nullptr;
}

void Tray::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto items = shown();
    for (int i = 0; i < int(items.size()); ++i) {
        const QRect cell = iconRect(i);
        if (items[size_t(i)] == m_hovered) {
            const int radius = tde::theme::radius();
            painter.setPen(Qt::NoPen);
            painter.setBrush(tde::theme::colors().hover);
            painter.drawRoundedRect(cell.adjusted(1, 3, -1, -3), radius, radius);
        }
        const QRect icon(
            cell.center().x() - IconSize / 2 + 1, cell.center().y() - IconSize / 2 + 1, IconSize, IconSize);
        items[size_t(i)]->icon().paint(&painter, icon);
    }
}

bool Tray::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (StatusNotifierItem* item = itemAt(help->pos()); item && !item->toolTip().isEmpty())
            QToolTip::showText(help->globalPos(), Qt::convertFromPlainText(item->toolTip()), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void Tray::mouseMoveEvent(QMouseEvent* event)
{
    StatusNotifierItem* item = itemAt(event->position().toPoint());
    if (item != m_hovered) {
        m_hovered = item;
        update();
    }
}

void Tray::leaveEvent(QEvent*)
{
    m_hovered = nullptr;
    update();
}

void Tray::mouseReleaseEvent(QMouseEvent* event)
{
    StatusNotifierItem* item = itemAt(event->position().toPoint());
    if (!item)
        return;
    // Menus open below the icon; programs that show windows of their own get the spot too.
    const auto items = shown();
    const int index = int(std::ranges::find(items, item) - items.begin());
    const QPoint pos = mapToGlobal(QPoint(iconRect(index).left(), height()));
    switch (event->button()) {
    case Qt::LeftButton:
        item->activate(pos, this);
        break;
    case Qt::MiddleButton:
        item->secondaryActivate(pos);
        break;
    case Qt::RightButton:
        item->contextMenu(pos, this);
        break;
    default:
        break;
    }
}

void Tray::wheelEvent(QWheelEvent* event)
{
    if (StatusNotifierItem* item = itemAt(event->position().toPoint())) {
        const QPoint delta = event->angleDelta();
        if (delta.y() != 0)
            item->scroll(delta.y(), Qt::Vertical);
        else if (delta.x() != 0)
            item->scroll(delta.x(), Qt::Horizontal);
    }
}

} // namespace hermes
