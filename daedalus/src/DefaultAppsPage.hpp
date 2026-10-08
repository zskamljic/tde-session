#pragma once

#include "Widgets.hpp"

#include <QString>
#include <QStringList>

namespace daedalus {

// The application a mimeapps.list's [Default Applications] names first for `mimeType`, if any.
QString defaultIn(const QString& mimeapps, const QString& mimeType);

// `mimeapps` with `appId` as the default for each of `mimeTypes`, and the rest as it was.
QString withDefault(const QString& mimeapps, const QStringList& mimeTypes, const QString& appId);

// The applications that open web pages, mail, music and the rest, as GNOME has them.
class DefaultAppsPage : public Page {
public:
    explicit DefaultAppsPage(QWidget* parent = nullptr);
};

} // namespace daedalus
