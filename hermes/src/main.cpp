#include "Bar.hpp"

#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"tde-hermes"_s);
    QApplication::setApplicationVersion(QStringLiteral(TDE_SESSION_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"The bar along the top of the screen."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    // One bar per session.
    if (!QDBusConnection::sessionBus().registerService(u"io.github.zskamljic.Hermes"_s)) {
        qWarning("tde-hermes: already running");
        return 0;
    }

    tde::createDesktopConfig(tde::desktopConfigPath());
    tde::setDesktop(tde::loadDesktopConfig());
    tde::theme::apply(app, tde::desktop().appearance);

    hermes::Bar bar;
    if (!bar.isSupported()) {
        qCritical("tde-hermes: the compositor does not let programs list windows");
        return 1;
    }
    bar.show();
    return QApplication::exec();
}
