#include "Icons.hpp"

#include "Applications.hpp"

#include <QFileInfo>
#include <QPainter>
#include <QPixmap>

using namespace Qt::StringLiterals;

namespace shell {

QIcon applicationIcon(const Application* app, const QString& fallbackName)
{
    const QIcon generic = QIcon::fromTheme(u"application-x-executable"_s);
    if (app && !app->icon.isEmpty()) {
        if (QFileInfo(app->icon).isAbsolute())
            return QIcon(app->icon);
        return QIcon::fromTheme(app->icon, generic);
    }
    return fallbackName.isEmpty() ? generic : QIcon::fromTheme(fallbackName, generic);
}

QIcon tintedIcon(const QString& name, QSize size, qreal ratio, const QColor& color)
{
    QPixmap pixmap = QIcon::fromTheme(name).pixmap(size, ratio);
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRect(QPoint(), pixmap.size()), color);
    painter.end();
    return QIcon(pixmap);
}

} // namespace shell
