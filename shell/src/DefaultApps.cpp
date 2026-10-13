#include "DefaultApps.hpp"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace shell {
namespace {

const QString Section = u"[Default Applications]"_s;

QString readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
}

// The names of the lists in a folder, as the specification orders them: those of the desktops
// in XDG_CURRENT_DESKTOP first, "tde-mimeapps.list", then the common one.
QStringList listNames()
{
    QStringList names;
    for (const QString& desktop : qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts))
        names << desktop.toLower() + u"-mimeapps.list"_s;
    names << u"mimeapps.list"_s;
    return names;
}

} // namespace

QString defaultIn(const QString& mimeapps, const QString& mimeType)
{
    bool inSection = false;
    for (const QString& raw : mimeapps.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith(u'[')) {
            inSection = line == Section;
            continue;
        }
        if (!inSection)
            continue;
        const qsizetype equals = line.indexOf(u'=');
        if (equals > 0 && line.left(equals).trimmed() == mimeType)
            return line.mid(equals + 1).split(u';', Qt::SkipEmptyParts).value(0).trimmed();
    }
    return {};
}

QString withDefault(const QString& mimeapps, const QStringList& mimeTypes, const QString& appId)
{
    QStringList lines = mimeapps.split(u'\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    qsizetype section = lines.indexOf(Section);
    if (section < 0) {
        if (!lines.isEmpty())
            lines << QString();
        lines << Section;
        section = lines.size() - 1;
    }
    // Where the section ends: at the next one, or at the end.
    qsizetype end = section + 1;
    while (end < lines.size() && !lines[end].trimmed().startsWith(u'['))
        ++end;
    for (const QString& type : mimeTypes) {
        const QString entry = type + u'=' + appId + u';';
        bool replaced = false;
        for (qsizetype i = section + 1; i < end; ++i) {
            const qsizetype equals = lines[i].indexOf(u'=');
            if (equals > 0 && lines[i].left(equals).trimmed() == type) {
                lines[i] = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            // After the section's last entry, not after the empty lines before the next one.
            qsizetype at = end;
            while (at > section + 1 && lines[at - 1].trimmed().isEmpty())
                --at;
            lines.insert(at, entry);
            ++end;
        }
    }
    return lines.join(u'\n') + u'\n';
}

QString userApplicationsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString defaultApplication(const QString& mimeType)
{
    QStringList lists;
    for (const QString& folder : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation)) {
        for (const QString& name : listNames())
            lists << folder + u'/' + name;
    }
    for (const QString& folder : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        for (const QString& name : listNames())
            lists << folder + u"/applications/"_s + name;
    }
    for (const QString& path : lists) {
        if (const QString found = defaultIn(readFile(path), mimeType); !found.isEmpty())
            return found;
    }
    return {};
}

bool setDefaultApplication(const QStringList& mimeTypes, const QString& appId)
{
    bool saved = true;
    for (const QString& name : listNames()) {
        const QString path = userApplicationsFolder() + u'/' + name;
        const QString text = readFile(path);
        const bool common = name == u"mimeapps.list";
        if (!common
            && std::ranges::none_of(mimeTypes, [&](const QString& type) { return !defaultIn(text, type).isEmpty(); }))
            continue;
        QDir().mkpath(userApplicationsFolder());
        QSaveFile file(path);
        saved = file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(withDefault(text, mimeTypes, appId).toUtf8()) >= 0 && file.commit() && saved;
    }
    return saved;
}

} // namespace shell
