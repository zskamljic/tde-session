#pragma once

#include <QPoint>
#include <QString>
#include <QVariantMap>

#include <vector>

class QDBusArgument;
class QMenu;
class QWidget;

namespace hermes {

// An entry of a menu a program exports over D-Bus (com.canonical.dbusmenu), with its
// entries below it.
struct MenuNode {
    int id = 0;
    QVariantMap properties;
    std::vector<MenuNode> children;
};

// Reads a layout as GetLayout returns it: (ia{sv}av), the children wrapped in variants.
MenuNode parseMenuLayout(const QDBusArgument& argument);

// A dbusmenu label as a Qt one: "_File" marks F, "__" is an underscore, "&" stays itself.
QString menuLabel(const QString& label);

// Fills `menu` with the entries below `node`, which report their clicks to the program.
void fillMenu(QMenu* menu, const MenuNode& node, const QString& service, const QString& path);

// Reads the menu at `path` of `service` and shows it at `pos`, in screen coordinates, as a
// popup of `parent`'s window.
void popUpDBusMenu(const QString& service, const QString& path, QPoint pos, QWidget* parent);

} // namespace hermes
