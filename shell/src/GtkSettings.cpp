#include "GtkSettings.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace shell {

bool setGtkSetting(const QString& path, const QString& key, const QString& value)
{
    QStringList lines;
    if (QFile file(path); file.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(file.readAll()).split(u'\n');
    // Without the last line's ending, which comes back when saved.
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    const QString wanted = key + u'=' + value;
    qsizetype group = -1; // the line of [Settings]
    qsizetype groupEnd = lines.size(); // the line after its last setting
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.startsWith(u'[')) {
            if (group >= 0) {
                groupEnd = i;
                break;
            }
            if (line == u"[Settings]")
                group = i;
            continue;
        }
        if (group < 0 || line.startsWith(u'#'))
            continue;
        const qsizetype equals = line.indexOf(u'=');
        if (equals > 0 && line.first(equals).trimmed() == key) {
            if (lines[i] == wanted)
                return true;
            lines[i] = wanted;
            group = -2; // done
            break;
        }
    }
    if (group == -1) {
        lines << u"[Settings]"_s << wanted;
    } else if (group >= 0) {
        // After the group's last setting, ahead of the blank lines before the next group.
        while (groupEnd > group + 1 && lines[groupEnd - 1].trimmed().isEmpty())
            --groupEnd;
        lines.insert(groupEnd, wanted);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write((lines.join(u'\n') + u'\n').toUtf8());
    return file.commit();
}

bool setGtkSettings(const QString& configDir, const QString& key, const QString& value)
{
    bool saved = true;
    for (const QString& gtk : {u"gtk-3.0"_s, u"gtk-4.0"_s})
        saved = setGtkSetting(u"%1/%2/settings.ini"_s.arg(configDir, gtk), key, value) && saved;
    return saved;
}

} // namespace shell
