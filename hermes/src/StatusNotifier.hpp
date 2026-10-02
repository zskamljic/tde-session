#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class QDBusArgument;
class QWidget;

namespace hermes {

// Where the registry of tray icons is on the session bus.
inline const QString WatcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
inline const QString WatcherPath = QStringLiteral("/StatusNotifierWatcher");

// An icon as status notifier items send it: ARGB32 pixels in network byte order.
struct IconPixmap {
    int width = 0;
    int height = 0;
    QByteArray data;
};
using IconPixmaps = QList<IconPixmap>;

QDBusArgument& operator<<(QDBusArgument& argument, const IconPixmap& pixmap);
const QDBusArgument& operator>>(const QDBusArgument& argument, IconPixmap& pixmap);

// The pixmaps as one icon, each size kept.
QIcon iconFromPixmaps(const IconPixmaps& pixmaps);

// Registers the D-Bus types above; call once before talking to items.
void registerStatusNotifierTypes();

// The registry of status notifier items: the icons programs used to put in the system tray,
// which KDE, the AppIndicator extension of GNOME and others show. Items register here, hosts
// (the tray) learn about them from here.
class Watcher : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isHostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    explicit Watcher(QObject* parent = nullptr);

    // Takes the watcher's name on the session bus; false when another program has it.
    bool start();

    // Items as "bus name/object path".
    QStringList registeredItems() const { return m_items; }
    bool isHostRegistered() const { return true; }
    int protocolVersion() const { return 0; }

public slots:
    // `service` is the item's bus name, or its object path on the caller's connection, as
    // libappindicator sends it.
    void RegisterStatusNotifierItem(const QString& service);
    void RegisterStatusNotifierHost(const QString& service);

signals:
    void StatusNotifierItemRegistered(const QString& item);
    void StatusNotifierItemUnregistered(const QString& item);
    void StatusNotifierHostRegistered();

private:
    void ownerGone(const QString& service);

    QStringList m_items;
    QDBusServiceWatcher m_owners;
};

// One item: its icon, title and tooltip, kept up to date, and what it does when clicked.
class StatusNotifierItem : public QObject {
    Q_OBJECT

public:
    StatusNotifierItem(const QString& service, const QString& path, QObject* parent = nullptr);

    QString key() const { return m_service + m_path; }
    QString title() const { return m_title; }
    QString toolTip() const { return m_toolTip.isEmpty() ? m_title : m_toolTip; }
    QIcon icon() const { return m_icon; }
    // Passive items have nothing to say just now and stay out of the tray.
    bool isPassive() const { return m_status == u"Passive"; }

    // Clicks at `pos`, in screen coordinates, on the icon in `parent`, whose window any menu
    // pops up from.
    void activate(QPoint pos, QWidget* parent);
    void secondaryActivate(QPoint pos);
    void contextMenu(QPoint pos, QWidget* parent);
    void scroll(int delta, Qt::Orientation orientation);

signals:
    void changed();

private slots:
    void scheduleRefresh();

private:
    void refresh();
    void apply(const QVariantMap& properties);
    QIcon themedIcon(const QString& name) const;
    QString findThemeFile(const QString& name) const;
    void showMenu(QPoint pos, QWidget* parent);
    void call(const QString& method, const QVariantList& arguments, std::function<void(bool)> done = {});

    QString m_service;
    QString m_path;
    QString m_title;
    QString m_toolTip;
    QString m_status;
    QString m_menuPath;
    QString m_themePath;
    bool m_itemIsMenu = false;
    QIcon m_icon;
    // Files found in the item's own icon folder, by name, as items that animate their icon
    // ask for the same few often; empty when there was none.
    mutable QHash<QString, QString> m_themeFiles;
    QTimer m_refresh;
};

} // namespace hermes

Q_DECLARE_METATYPE(hermes::IconPixmap)
