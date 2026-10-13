#pragma once

#include "Widgets.hpp"

#include <QString>
#include <QStringList>

namespace daedalus {

// The applications that open web pages, mail, music and the rest, as GNOME has them.
class DefaultAppsPage : public Page {
public:
    explicit DefaultAppsPage(QWidget* parent = nullptr);
};

} // namespace daedalus
