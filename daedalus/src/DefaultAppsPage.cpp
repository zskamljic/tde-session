#include "DefaultAppsPage.hpp"

#include <Applications.hpp>
#include <DefaultApps.hpp>
#include <DesktopSettings.hpp>
#include <Icons.hpp>
#include <tde/DesktopConfig.hpp>
#include <tde/LuaConfig.hpp>

#include <QComboBox>
#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

struct Kind {
    const char* title;
    QStringList mimeTypes; // the first one picks the applications offered
};

// The program a terminal application runs, which the desktop config names.
QString programOf(const shell::Application& app)
{
    return QFileInfo(shell::splitExec(app.exec).value(0)).fileName();
}

} // namespace

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
        const QString current = shell::defaultApplication(kind.mimeTypes.first());
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
            if (!id.isEmpty() && !shell::setDefaultApplication(types, id))
                qWarning("tde-daedalus: cannot write the default applications to %s",
                    qPrintable(shell::userApplicationsFolder()));
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
        if (app.categories.contains(u"TerminalEmulator"_s) && app.inMenus && program != u"flatpak" && program != u"env")
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
