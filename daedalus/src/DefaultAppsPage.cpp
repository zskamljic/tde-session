#include "DefaultAppsPage.hpp"

#include <Applications.hpp>
#include <Icons.hpp>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

const QString Section = u"[Default Applications]"_s;

struct Kind {
    const char* title;
    QStringList mimeTypes; // the first one picks the applications offered
};

QString readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
}

QString userList()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/mimeapps.list"_s;
}

// The default for `mimeType` as the lists say it, the user's first, then the system's.
QString currentDefault(const QString& mimeType)
{
    QStringList lists {userList()};
    for (const QString& folder : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation))
        lists << folder + u"/mimeapps.list"_s;
    for (const QString& folder : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        lists << folder + u"/applications/mimeapps.list"_s;
    for (const QString& path : lists) {
        if (const QString found = defaultIn(readFile(path), mimeType); !found.isEmpty())
            return found;
    }
    return {};
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

DefaultAppsPage::DefaultAppsPage(QWidget* parent)
    : Page(parent)
{
    const std::vector<Kind> kinds {
        {"Web", {u"x-scheme-handler/http"_s, u"x-scheme-handler/https"_s, u"text/html"_s}},
        {"Mail", {u"x-scheme-handler/mailto"_s}},
        {"Calendar", {u"text/calendar"_s}},
        {"Music", {u"audio/mpeg"_s, u"audio/flac"_s, u"audio/x-vorbis+ogg"_s, u"audio/x-wav"_s}},
        {"Video", {u"video/mp4"_s, u"video/x-matroska"_s, u"video/webm"_s}},
        {"Photos", {u"image/jpeg"_s, u"image/png"_s, u"image/gif"_s, u"image/webp"_s}},
        {"Files", {u"inode/directory"_s}},
    };
    const auto apps = shell::loadApplications(qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':'));

    Group* group = addGroup(u"Default Apps"_s);
    for (const Kind& kind : kinds) {
        const QString current = currentDefault(kind.mimeTypes.first());
        auto* box = new QComboBox(this);
        box->setMinimumWidth(240);
        for (const shell::Application& app : apps) {
            if (!app.mimeTypes.contains(kind.mimeTypes.first()) || (!app.inMenus && app.id != current))
                continue;
            box->addItem(shell::applicationIcon(&app, app.icon), app.name, app.id);
        }
        if (box->count() == 0) {
            box->addItem(u"No application"_s);
            box->setEnabled(false);
        } else if (const int index = box->findData(current); index >= 0) {
            box->setCurrentIndex(index);
        } else {
            // None is chosen yet: the system picks one by itself.
            box->insertItem(0, u"Not chosen"_s);
            box->setCurrentIndex(0);
        }
        connect(box, &QComboBox::activated, this, [box, types = kind.mimeTypes] {
            const QString id = box->currentData().toString();
            if (id.isEmpty())
                return;
            const QString path = userList();
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
                || file.write(withDefault(readFile(path), types, id).toUtf8()) < 0 || !file.commit())
                qWarning("tde-daedalus: cannot write %s", qPrintable(path));
        });
        group->addRow(QString::fromUtf8(kind.title), {}, box);
    }
    group->addNote(u"Each opens what is of its kind: web links and pages, mail addresses, events, and the rest."_s);
}

} // namespace daedalus
