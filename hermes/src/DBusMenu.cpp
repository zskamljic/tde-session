#include "DBusMenu.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QIcon>
#include <QMenu>
#include <QPixmap>

#include <functional>
#include <memory>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

const QString MenuInterface = u"com.canonical.dbusmenu"_s;

void sendEvent(const QString& service, const QString& path, int id, const QString& event)
{
    auto message = QDBusMessage::createMethodCall(service, path, MenuInterface, u"Event"_s);
    message << id << event << QVariant::fromValue(QDBusVariant(0)) << uint(QDateTime::currentSecsSinceEpoch());
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
}

QIcon menuIcon(const QVariantMap& properties)
{
    const QByteArray data = properties.value(u"icon-data"_s).toByteArray();
    if (QPixmap pixmap; !data.isEmpty() && pixmap.loadFromData(data))
        return QIcon(pixmap);
    const QString name = properties.value(u"icon-name"_s).toString();
    return name.isEmpty() ? QIcon() : QIcon::fromTheme(name);
}

// Reads the entries below `id`, after letting the program fill them in, as programs may do
// only now. `done` runs with them unless `context` is gone by then.
void readLayout(
    const QString& service, const QString& path, int id, QObject* context, std::function<void(const MenuNode&)> done)
{
    auto bus = QDBusConnection::sessionBus();
    auto aboutToShow = QDBusMessage::createMethodCall(service, path, MenuInterface, u"AboutToShow"_s);
    aboutToShow << id;
    bus.call(aboutToShow, QDBus::NoBlock);

    auto getLayout = QDBusMessage::createMethodCall(service, path, MenuInterface, u"GetLayout"_s);
    getLayout << id << -1 << QStringList();
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(getLayout), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
        [service, done = std::move(done)](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusMessage reply = call->reply();
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
                qWarning("tde-hermes: could not read the menu of %s: %s", qPrintable(service),
                    qPrintable(reply.errorMessage()));
                return;
            }
            done(parseMenuLayout(reply.arguments().at(1).value<QDBusArgument>()));
        });
}

} // namespace

MenuNode parseMenuLayout(const QDBusArgument& argument)
{
    MenuNode node;
    argument.beginStructure();
    argument >> node.id >> node.properties;
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant child;
        argument >> child;
        node.children.push_back(parseMenuLayout(child.variant().value<QDBusArgument>()));
    }
    argument.endArray();
    argument.endStructure();
    return node;
}

QString menuLabel(const QString& label)
{
    QString result;
    result.reserve(label.size());
    for (qsizetype i = 0; i < label.size(); ++i) {
        const QChar c = label[i];
        if (c == u'&') {
            result += u"&&";
        } else if (c == u'_' && i + 1 < label.size() && label[i + 1] == u'_') {
            result += u'_';
            ++i;
        } else if (c == u'_') {
            result += u'&';
        } else {
            result += c;
        }
    }
    return result;
}

void fillMenu(QMenu* menu, const MenuNode& node, const QString& service, const QString& path)
{
    for (const MenuNode& child : node.children) {
        const QVariantMap& properties = child.properties;
        if (!properties.value(u"visible"_s, true).toBool())
            continue;
        if (properties.value(u"type"_s).toString() == u"separator") {
            menu->addSeparator();
            continue;
        }

        const QString label = menuLabel(properties.value(u"label"_s).toString());
        const bool enabled = properties.value(u"enabled"_s, true).toBool();
        if (properties.value(u"children-display"_s).toString() == u"submenu" || !child.children.empty()) {
            QMenu* submenu = menu->addMenu(menuIcon(properties), label);
            submenu->setEnabled(enabled);
            const int id = child.id;
            // Filled with what is known, and again with what the program has once it opens.
            QObject::connect(submenu, &QMenu::aboutToShow, submenu, [submenu, service, path, id] {
                sendEvent(service, path, id, u"opened"_s);
                readLayout(service, path, id, submenu, [submenu, service, path](const MenuNode& node) {
                    // Menus inside it are its children, which clearing leaves behind.
                    qDeleteAll(submenu->findChildren<QMenu*>(Qt::FindDirectChildrenOnly));
                    submenu->clear();
                    fillMenu(submenu, node, service, path);
                });
            });
            fillMenu(submenu, child, service, path);
            continue;
        }

        QAction* action = menu->addAction(menuIcon(properties), label);
        action->setEnabled(enabled);
        if (!properties.value(u"toggle-type"_s).toString().isEmpty()) {
            action->setCheckable(true);
            action->setChecked(properties.value(u"toggle-state"_s).toInt() == 1);
        }
        const int id = child.id;
        QObject::connect(
            action, &QAction::triggered, action, [service, path, id] { sendEvent(service, path, id, u"clicked"_s); });
    }
}

void popUpDBusMenu(const QString& service, const QString& path, QPoint pos, QWidget* parent)
{
    readLayout(service, path, 0, parent, [service, path, pos, parent](const MenuNode& root) {
        // Popups on Wayland belong to a window.
        auto owned = std::make_unique<QMenu>(parent);
        fillMenu(owned.get(), root, service, path);
        if (owned->isEmpty())
            return;
        // It goes once closed.
        QMenu* menu = owned.release();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        sendEvent(service, path, 0, u"opened"_s);
        QObject::connect(menu, &QMenu::aboutToHide, [service, path] { sendEvent(service, path, 0, u"closed"_s); });
        menu->popup(pos);
    });
}

} // namespace hermes
