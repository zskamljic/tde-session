#include "StatusNotifier.hpp"

#include "DBusMenu.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

const QString ItemInterface = u"org.kde.StatusNotifierItem"_s;

// The tooltip's title, and its text when there is one: (s icon, a(iiay) pixmaps, s title,
// s text).
QString toolTipText(const QVariant& value)
{
    if (!value.canConvert<QDBusArgument>())
        return {};
    const auto argument = value.value<QDBusArgument>();
    QString iconName;
    IconPixmaps pixmaps;
    QString title;
    QString text;
    argument.beginStructure();
    argument >> iconName >> pixmaps >> title >> text;
    argument.endStructure();
    return text.isEmpty() ? title : title.isEmpty() ? text : title + u'\n' + text;
}

IconPixmaps pixmapsOf(const QVariant& value)
{
    if (!value.canConvert<QDBusArgument>())
        return {};
    return qdbus_cast<IconPixmaps>(value.value<QDBusArgument>());
}

} // namespace

QDBusArgument& operator<<(QDBusArgument& argument, const IconPixmap& pixmap)
{
    argument.beginStructure();
    argument << pixmap.width << pixmap.height << pixmap.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, IconPixmap& pixmap)
{
    argument.beginStructure();
    argument >> pixmap.width >> pixmap.height >> pixmap.data;
    argument.endStructure();
    return argument;
}

QIcon iconFromPixmaps(const IconPixmaps& pixmaps)
{
    QIcon icon;
    for (const IconPixmap& pixmap : pixmaps) {
        if (pixmap.width <= 0 || pixmap.height <= 0 || pixmap.data.size() < qsizetype(pixmap.width) * pixmap.height * 4)
            continue;
        QImage image(pixmap.width, pixmap.height, QImage::Format_ARGB32);
        const auto* source = reinterpret_cast<const uchar*>(pixmap.data.constData());
        for (int y = 0; y < pixmap.height; ++y) {
            auto* line = reinterpret_cast<quint32*>(image.scanLine(y));
            for (int x = 0; x < pixmap.width; ++x, source += 4)
                line[x] = qFromBigEndian<quint32>(source);
        }
        icon.addPixmap(QPixmap::fromImage(std::move(image)));
    }
    return icon;
}

void registerStatusNotifierTypes()
{
    qDBusRegisterMetaType<IconPixmap>();
    qDBusRegisterMetaType<IconPixmaps>();
}

// Watcher -------------------------------------------------------------------------------

Watcher::Watcher(QObject* parent)
    : QObject(parent)
{
    m_owners.setConnection(QDBusConnection::sessionBus());
    m_owners.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_owners, &QDBusServiceWatcher::serviceUnregistered, this, &Watcher::ownerGone);
}

bool Watcher::start()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(WatcherService))
        return false;
    return bus.registerObject(WatcherPath, this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties);
}

void Watcher::RegisterStatusNotifierItem(const QString& service)
{
    // A path means the item lives on the caller's own connection.
    const bool isPath = service.startsWith(u'/');
    const QString busName = isPath ? message().service() : service;
    const QString item = busName + (isPath ? service : u"/StatusNotifierItem"_s);
    if (m_items.contains(item))
        return;
    m_items << item;
    m_owners.addWatchedService(busName);
    emit StatusNotifierItemRegistered(item);
}

void Watcher::RegisterStatusNotifierHost(const QString&)
{
    emit StatusNotifierHostRegistered();
}

void Watcher::ownerGone(const QString& service)
{
    m_owners.removeWatchedService(service);
    for (const QString& item : QStringList(m_items)) {
        if (item.section(u'/', 0, 0) == service) {
            m_items.removeAll(item);
            emit StatusNotifierItemUnregistered(item);
        }
    }
}

// Items ---------------------------------------------------------------------------------

StatusNotifierItem::StatusNotifierItem(const QString& service, const QString& path, QObject* parent)
    : QObject(parent)
    , m_service(service)
    , m_path(path)
{
    // Items often change several things at once; read them once they are done.
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(50);
    connect(&m_refresh, &QTimer::timeout, this, &StatusNotifierItem::refresh);

    auto bus = QDBusConnection::sessionBus();
    for (const char* signal : {"NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewToolTip", "NewStatus"})
        bus.connect(m_service, m_path, ItemInterface, QString::fromLatin1(signal), this, SLOT(scheduleRefresh()));
    refresh();
}

void StatusNotifierItem::scheduleRefresh()
{
    m_refresh.start();
}

void StatusNotifierItem::refresh()
{
    auto message = QDBusMessage::createMethodCall(m_service, m_path, u"org.freedesktop.DBus.Properties"_s, u"GetAll"_s);
    message << ItemInterface;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *call;
        if (reply.isError()) {
            qWarning("tde-hermes: could not read the tray icon of %s: %s", qPrintable(m_service),
                qPrintable(reply.error().message()));
            return;
        }
        apply(reply.value());
    });
}

// The icon `name` in the item's own icon folder, laid out as an icon theme is: directly in it,
// or a few folders down, as in hicolor/22x22/apps. No deeper, as the folder is the item's to
// choose and could be the whole disk.
QString StatusNotifierItem::findThemeFile(const QString& name) const
{
    const QStringList files {name + u".png"_s, name + u".svg"_s};
    QStringList folders {m_themePath};
    for (int depth = 0; depth <= 3 && !folders.isEmpty(); ++depth) {
        QStringList deeper;
        for (const QString& folder : std::as_const(folders)) {
            const QDir dir(folder);
            for (const QString& file : dir.entryList(files, QDir::Files))
                return dir.filePath(file);
            for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                deeper << dir.filePath(sub);
        }
        folders = deeper.mid(0, 200);
    }
    return {};
}

QIcon StatusNotifierItem::themedIcon(const QString& name) const
{
    if (name.isEmpty())
        return {};
    if (name.startsWith(u'/'))
        return QIcon(name);
    // Items may bring their own icons, in a folder the icon theme does not know.
    if (!m_themePath.isEmpty()) {
        const auto cached = m_themeFiles.constFind(name);
        const QString file = cached != m_themeFiles.cend() ? *cached : findThemeFile(name);
        m_themeFiles.insert(name, file);
        if (!file.isEmpty())
            return QIcon(file);
    }
    return QIcon::fromTheme(name);
}

void StatusNotifierItem::apply(const QVariantMap& properties)
{
    m_title = properties.value(u"Title"_s).toString();
    m_status = properties.value(u"Status"_s).toString();
    if (const QString themePath = properties.value(u"IconThemePath"_s).toString(); themePath != m_themePath) {
        m_themePath = themePath;
        m_themeFiles.clear();
    }
    m_itemIsMenu = properties.value(u"ItemIsMenu"_s).toBool();
    m_menuPath = properties.value(u"Menu"_s).value<QDBusObjectPath>().path();
    m_toolTip = toolTipText(properties.value(u"ToolTip"_s));

    const bool attention = m_status == u"NeedsAttention";
    QIcon icon;
    if (attention) {
        icon = themedIcon(properties.value(u"AttentionIconName"_s).toString());
        if (icon.isNull())
            icon = iconFromPixmaps(pixmapsOf(properties.value(u"AttentionIconPixmap"_s)));
    }
    if (icon.isNull())
        icon = themedIcon(properties.value(u"IconName"_s).toString());
    if (icon.isNull())
        icon = iconFromPixmaps(pixmapsOf(properties.value(u"IconPixmap"_s)));
    if (icon.isNull())
        icon = QIcon::fromTheme(u"application-x-executable"_s);
    m_icon = icon;
    emit changed();
}

void StatusNotifierItem::call(const QString& method, const QVariantList& arguments, std::function<void(bool)> done)
{
    auto message = QDBusMessage::createMethodCall(m_service, m_path, ItemInterface, method);
    message.setArguments(arguments);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [done = std::move(done)](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (done)
            done(!call->isError());
    });
}

void StatusNotifierItem::showMenu(QPoint pos, QWidget* parent)
{
    if (!m_menuPath.isEmpty() && m_menuPath != u"/")
        popUpDBusMenu(m_service, m_menuPath, pos, parent);
}

void StatusNotifierItem::activate(QPoint pos, QWidget* parent)
{
    if (m_itemIsMenu && !m_menuPath.isEmpty()) {
        showMenu(pos, parent);
        return;
    }
    // Items that only have a menu answer Activate with an error.
    call(u"Activate"_s, {pos.x(), pos.y()}, [this, pos, parent = QPointer(parent)](bool ok) {
        if (!ok && parent)
            showMenu(pos, parent);
    });
}

void StatusNotifierItem::secondaryActivate(QPoint pos)
{
    call(u"SecondaryActivate"_s, {pos.x(), pos.y()});
}

void StatusNotifierItem::contextMenu(QPoint pos, QWidget* parent)
{
    if (!m_menuPath.isEmpty() && m_menuPath != u"/")
        showMenu(pos, parent);
    else
        call(u"ContextMenu"_s, {pos.x(), pos.y()});
}

void StatusNotifierItem::scroll(int delta, Qt::Orientation orientation)
{
    call(u"Scroll"_s, {delta, orientation == Qt::Horizontal ? u"horizontal"_s : u"vertical"_s});
}

} // namespace hermes
