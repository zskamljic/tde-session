#include "Flip.hpp"
#include "Overview.hpp"

#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>

using namespace Qt::StringLiterals;

namespace {

const QString Service = u"io.github.zskamljic.Argus"_s;
const QString Path = u"/io/github/zskamljic/Argus"_s;
const QString FlipPath = u"/io/github/zskamljic/Argus/Flip"_s;
const QString FlipInterface = u"io.github.zskamljic.Argus.Flip"_s;

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"tde-argus"_s);
    QApplication::setApplicationVersion(QStringLiteral(TDE_SESSION_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Every open window at a glance."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption toggleOption(u"toggle"_s, u"Show the overview, or hide it when shown."_s);
    parser.addOption(toggleOption);
    const QCommandLineOption applicationsOption(
        u"applications"_s, u"Show all applications, or hide the overview when they are shown."_s);
    parser.addOption(applicationsOption);
    const QCommandLineOption flipOption(u"flip"_s, u"Flip through the windows, or to the next one when shown."_s);
    parser.addOption(flipOption);
    parser.process(app);
    const bool toggle = parser.isSet(toggleOption);
    const bool applications = parser.isSet(applicationsOption);
    const bool flip = parser.isSet(flipOption);

    // One overview per session: later starts hand their request to it.
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(Service)) {
        // Waiting for the answer, so the request is out before this program is.
        if (flip) {
            auto message = QDBusMessage::createMethodCall(Service, FlipPath, FlipInterface, u"Show"_s);
            message << false;
            bus.call(message);
        } else if (toggle || applications) {
            bus.call(QDBusMessage::createMethodCall(
                Service, Path, Service, applications ? u"ToggleApplications"_s : u"Toggle"_s));
        }
        return 0;
    }

    tde::createDesktopConfig(tde::desktopConfigPath());
    tde::setDesktop(tde::loadDesktopConfig());
    tde::theme::apply(app, tde::desktop().appearance);

    argus::Toplevels toplevels;
    if (!toplevels.isSupported()) {
        qCritical("tde-argus: the compositor does not let programs list windows");
        return 1;
    }
    argus::Overview overview(toplevels);
    argus::Flip flipper(toplevels);
    bus.registerObject(Path, &overview, QDBusConnection::ExportScriptableSlots);
    bus.registerObject(FlipPath, &flipper, QDBusConnection::ExportScriptableSlots);
    if (applications)
        overview.ToggleApplications();
    else if (toggle)
        overview.Show();
    else if (flip)
        flipper.Show(false);
    return QApplication::exec();
}
