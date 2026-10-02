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
    parser.process(app);
    const bool toggle = parser.isSet(toggleOption);
    const bool applications = parser.isSet(applicationsOption);

    // One overview per session: later starts hand their request to it.
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(Service)) {
        // Waiting for the answer, so the request is out before this program is.
        if (toggle || applications)
            bus.call(QDBusMessage::createMethodCall(
                Service, Path, Service, applications ? u"ToggleApplications"_s : u"Toggle"_s));
        return 0;
    }

    tde::createDesktopConfig(tde::desktopConfigPath());
    tde::setDesktop(tde::loadDesktopConfig());
    tde::theme::apply(app, tde::desktop().appearance);

    argus::Overview overview;
    if (!overview.isSupported()) {
        qCritical("tde-argus: the compositor does not let programs list windows");
        return 1;
    }
    bus.registerObject(Path, &overview, QDBusConnection::ExportScriptableSlots);
    if (applications)
        overview.ToggleApplications();
    else if (toggle)
        overview.Show();
    return QApplication::exec();
}
