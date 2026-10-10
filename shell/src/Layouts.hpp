#pragma once

#include <QLayout>
#include <QWidget>

#include <memory>

namespace shell {

// Takes the items out of `layout` but the last `keep`. Their widgets hide at once and go once
// control is back in the event loop, as the button that asked for this may be among them.
// Header-only, for the programs with widgets: the shell library has none.
inline void clearLayout(QLayout& layout, int keep = 0)
{
    while (layout.count() > keep) {
        const std::unique_ptr<QLayoutItem> item(layout.takeAt(0));
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
    }
}

} // namespace shell
