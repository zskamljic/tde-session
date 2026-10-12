#include "Themes.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

const QString Interface = u"org.gnome.desktop.interface"_s;

QStringList dataFolders(const QString& name)
{
    QStringList folders {QDir::homePath() + u"/."_s + name};
    for (const QString& data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        folders << data + u'/' + name;
    folders.removeDuplicates();
    return folders;
}

void sortByName(std::vector<Theme>& themes)
{
    std::ranges::sort(themes, [](const Theme& a, const Theme& b) { return a.name.localeAwareCompare(b.name) < 0; });
}

// Every theme folder in `folders` that `accept` takes, the first of each name only, by name.
template <typename Accept> std::vector<Theme> themesIn(const QStringList& folders, Accept accept)
{
    std::vector<Theme> themes;
    QStringList seen;
    for (const QString& folder : folders) {
        const QDir dir(folder);
        for (const QString& id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (seen.contains(id))
                continue;
            const QString path = dir.filePath(id);
            const QSettings index(path + u"/index.theme"_s, QSettings::IniFormat);
            QString name;
            if (!accept(path, index, name))
                continue;
            seen << id;
            themes.push_back({id, name.isEmpty() ? id : name});
        }
    }
    sortByName(themes);
    return themes;
}

} // namespace

QStringList iconFolders()
{
    return dataFolders(u"icons"_s);
}

QStringList themeFolders()
{
    return dataFolders(u"themes"_s);
}

std::vector<Theme> iconThemes(const QStringList& folders)
{
    return themesIn(folders, [](const QString& path, const QSettings& index, QString& name) {
        // Only themes of icons list folders of them; "hicolor" is what every one falls back on.
        if (index.value(u"Icon Theme/Directories"_s).toStringList().isEmpty()
            || index.value(u"Icon Theme/Hidden"_s).toBool() || QFileInfo(path).fileName() == u"hicolor")
            return false;
        name = index.value(u"Icon Theme/Name"_s).toString();
        return true;
    });
}

std::vector<Theme> cursorThemes(const QStringList& folders)
{
    return themesIn(folders, [](const QString& path, const QSettings& index, QString& name) {
        if (!QFileInfo(path + u"/cursors"_s).isDir())
            return false;
        name = index.value(u"Icon Theme/Name"_s).toString();
        // Icon themes with pointers too call themselves by the name of their icons.
        if (name.isEmpty() || !index.value(u"Icon Theme/Directories"_s).toStringList().isEmpty())
            name = QFileInfo(path).fileName();
        return true;
    });
}

std::vector<Theme> gtkThemes(const QStringList& folders)
{
    std::vector<Theme> themes = themesIn(folders, [](const QString& path, const QSettings&, QString&) {
        return QFileInfo::exists(path + u"/gtk-3.0/gtk.css"_s);
    });
    for (const QString& builtIn : {u"Adwaita"_s, u"Adwaita-dark"_s}) {
        if (!std::ranges::contains(themes, builtIn, &Theme::id))
            themes.push_back({builtIn, builtIn});
    }
    sortByName(themes);
    return themes;
}

QString gnomeSetting(const QString& key)
{
    QProcess gsettings;
    gsettings.setStandardInputFile(QProcess::nullDevice());
    gsettings.setStandardErrorFile(QProcess::nullDevice());
    gsettings.start(u"gsettings"_s, {u"get"_s, Interface, key});
    if (!gsettings.waitForFinished(2000) || gsettings.exitStatus() != QProcess::NormalExit || gsettings.exitCode() != 0)
        return {};
    QString value = QString::fromUtf8(gsettings.readAllStandardOutput()).trimmed();
    // Strings are printed in single quotes; numbers with their type before them at times.
    if (value.size() >= 2 && value.front() == u'\'' && value.back() == u'\'')
        return value.mid(1, value.size() - 2);
    return value.section(u' ', -1);
}

void setGnomeSetting(const QString& key, const QString& value)
{
    QProcess::startDetached(u"gsettings"_s, {u"set"_s, Interface, key, value});
}

void setGnomeString(const QString& key, const QString& value)
{
    // gsettings reads a word without quotes as a string too, but not every name is one.
    QString quoted = value;
    quoted.replace(u'\\', u"\\\\"_s).replace(u'\'', u"\\'"_s);
    setGnomeSetting(key, u'\'' + quoted + u'\'');
}

} // namespace daedalus
