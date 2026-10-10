#include "DefaultAppsPage.hpp"

#include <Applications.hpp>
#include <DesktopSettings.hpp>
#include <Icons.hpp>
#include <tde/DesktopConfig.hpp>
#include <tde/LuaConfig.hpp>

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

QString userFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
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

// The default for `mimeType` as the lists say it, the user's first, then the system's.
QString currentDefault(const QString& mimeType)
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

// Makes `appId` the default for `mimeTypes` in the user's common list, and in the lists of
// the desktop that would come before it, where they have a say.
bool setDefault(const QStringList& mimeTypes, const QString& appId)
{
    bool saved = true;
    for (const QString& name : listNames()) {
        const QString path = userFolder() + u'/' + name;
        const QString text = readFile(path);
        const bool common = name == u"mimeapps.list";
        if (!common
            && std::ranges::none_of(mimeTypes, [&](const QString& type) { return !defaultIn(text, type).isEmpty(); }))
            continue;
        QDir().mkpath(userFolder());
        QSaveFile file(path);
        saved = file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(withDefault(text, mimeTypes, appId).toUtf8()) >= 0 && file.commit() && saved;
    }
    return saved;
}

// The program a terminal application runs, which the desktop config names.
QString programOf(const shell::Application& app)
{
    return QFileInfo(shell::splitExec(app.exec).value(0)).fileName();
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
        {"Documents", {u"application/pdf"_s}},
        {"Text", {u"text/plain"_s}},
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
            if (!id.isEmpty() && !setDefault(types, id))
                qWarning("tde-daedalus: cannot write the default applications to %s", qPrintable(userFolder()));
        });
        group->addRow(QString::fromUtf8(kind.title), {}, box);
    }
    group->addNote(u"Each opens what is of its kind: web links and pages, mail addresses, events, and the rest."_s);

    // The terminal is the desktop's choice, for "Open in Terminal" and programs that run in one.
    Group* tools = addGroup(u"Tools"_s);
    auto* terminal = new QComboBox(this);
    terminal->setMinimumWidth(240);
    terminal->addItem(u"The first one installed"_s, QString());
    const QString chosen = tde::loadDesktopConfig().terminal;
    for (const shell::Application& app : apps) {
        // Those run through another program, as Flatpak's are, cannot be named by theirs.
        const QString program = programOf(app);
        if (app.categories.contains(u"TerminalEmulator"_s) && app.inMenus && program != u"flatpak"
            && program != u"env")
            terminal->addItem(shell::applicationIcon(&app, app.icon), app.name, program);
    }
    if (!chosen.isEmpty() && terminal->findData(chosen) < 0)
        terminal->addItem(chosen, chosen);
    terminal->setCurrentIndex(std::max(0, terminal->findData(chosen)));
    connect(terminal, &QComboBox::activated, this, [terminal] {
        const QString program = terminal->currentData().toString();
        // None chosen leaves it to $TERMINAL and the usual ones.
        if (!shell::setDesktopSetting(
                tde::desktopConfigPath(), {}, u"terminal"_s, program.isEmpty() ? u"nil"_s : tde::luaString(program)))
            qWarning("tde-daedalus: cannot write %s", qPrintable(tde::desktopConfigPath()));
    });
    tools->addRow(u"Terminal"_s, u"Opens folders and runs programs that need one"_s, terminal);
}

} // namespace daedalus
