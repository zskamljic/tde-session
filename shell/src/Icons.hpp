#pragma once

#include <QIcon>

class QColor;

namespace shell {

struct Application;

// The icon of `app`, from the icon theme or a file. Without one, the theme's icon called
// `fallbackName`, which windows of programs not installed as applications often have, and
// last a generic program icon.
QIcon applicationIcon(const Application* app, const QString& fallbackName = {});

// The symbolic icon `name` drawn in `color`, as GTK recolours them; Qt draws them as they
// are, dark on dark.
QIcon tintedIcon(const QString& name, QSize size, qreal ratio, const QColor& color);

} // namespace shell
