#pragma once

#include <QString>
#include <QStringList>

// The applications that open web pages, mail, music and the rest, as the mimeapps.list files
// of the XDG specification name them.
namespace shell {

// The application a mimeapps.list's [Default Applications] names first for `mimeType`, if any.
QString defaultIn(const QString& mimeapps, const QString& mimeType);

// `mimeapps` with `appId` as the default for each of `mimeTypes`, and the rest as it was.
QString withDefault(const QString& mimeapps, const QStringList& mimeTypes, const QString& appId);

// Where the user's lists are.
QString userApplicationsFolder();

// The id of the default application for `mimeType` as the lists say it, the user's first, then
// the system's; empty when none says.
QString defaultApplication(const QString& mimeType);

// Makes `appId` the default for `mimeTypes` in the user's common list, and in the lists of
// the desktop that would come before it, where they have a say.
bool setDefaultApplication(const QStringList& mimeTypes, const QString& appId);

} // namespace shell
