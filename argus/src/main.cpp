#include "Background.hpp"
#include "Desktop.hpp"
#include "Flip.hpp"
#include "Screenshot.hpp"
#include "Switcher.hpp"

#include <Platform.hpp>
#include <SessionConfig.hpp>
#include <tde/ConfigWatcher.hpp>
#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

const QString Service = u"io.github.zskamljic.Argus"_s;
const QString Path = u"/io/github/zskamljic/Argus"_s;

QDBusMessage call(const QString& part, const QString& method)
{
    const QString path = part.isEmpty() ? Path : Path + u'/' + part;
    const QString interface = part.isEmpty() ? Service : Service + u'.' + part;
    return QDBusMessage::createMethodCall(Service, path, interface, method);
}

// A switcher opening forwards, with every window.
QDBusMessage pick(const QString& part)
{
    QDBusMessage message = call(part, u"Show"_s);
    message << false << false;
    return message;
}

} // namespace

int main(int argc, char* argv[])
{
    shell::keepOffPortal();
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"tde-argus"_s);
    QApplication::setApplicationVersion(QStringLiteral(TDE_SESSION_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"Every open window at a glance, the desktop below them, and switching between them."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption toggleOption(u"toggle"_s, u"Show the overview, or hide it when shown."_s);
    const QCommandLineOption applicationsOption(
        u"applications"_s, u"Show all applications, or hide the overview when they are shown."_s);
    const QCommandLineOption flipOption(u"flip"_s, u"Flip through the windows, or to the next one when shown."_s);
    const QCommandLineOption switchOption(u"switch"_s, u"Switch windows, or pick the next one when switching."_s);
    const QCommandLineOption screenshotOption(u"screenshot"_s, u"Take a screenshot of what is picked on the screen."_s);
    const QCommandLineOption screenOption(u"screenshot-screen"_s, u"Take a screenshot of the screens."_s);
    const QCommandLineOption windowOption(u"screenshot-window"_s, u"Take a screenshot of the window in use."_s);
    parser.addOptions(
        {toggleOption, applicationsOption, flipOption, switchOption, screenshotOption, screenOption, windowOption});
    parser.process(app);

    std::optional<QDBusMessage> request;
    if (parser.isSet(applicationsOption))
        request = call({}, u"ToggleApplications"_s);
    else if (parser.isSet(toggleOption))
        request = call({}, u"Toggle"_s);
    else if (parser.isSet(flipOption))
        request = pick(u"Flip"_s);
    else if (parser.isSet(switchOption))
        request = pick(u"Switcher"_s);
    else if (parser.isSet(screenshotOption))
        request = call(u"Screenshot"_s, u"Show"_s);
    else if (parser.isSet(screenOption))
        request = call(u"Screenshot"_s, u"TakeScreen"_s);
    else if (parser.isSet(windowOption))
        request = call(u"Screenshot"_s, u"TakeWindow"_s);

    // One per session: later starts hand their request to it, and wait for the answer, so the
    // request is out before they are.
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(Service)) {
        if (request)
            bus.call(*request);
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

    argus::Wallpaper wallpaper;
    argus::Desktop desktop(toplevels, wallpaper);
    argus::Flip flip(toplevels, wallpaper);
    argus::Switcher switcher(toplevels);
    argus::Screenshot screenshot(toplevels);
    // The switchers and the screenshot's buttons show where the bar is.
    const auto placeSwitchers = [&](QScreen* screen) {
        flip.setScreen(screen);
        switcher.setScreen(screen);
        screenshot.setPrimaryScreen(screen);
    };
    QObject::connect(&desktop, &argus::Desktop::primaryScreenChanged, placeSwitchers);
    placeSwitchers(desktop.primaryScreen());
    bus.registerObject(Path, &desktop, QDBusConnection::ExportScriptableSlots);
    bus.registerObject(Path + u"/Flip"_s, &flip, QDBusConnection::ExportScriptableSlots);
    bus.registerObject(Path + u"/Switcher"_s, &switcher, QDBusConnection::ExportScriptableSlots);
    bus.registerObject(Path + u"/Screenshot"_s, &screenshot, QDBusConnection::ExportScriptableSlots);

    // The settings take effect as soon as they are saved.
    const auto applySettings = [&] {
        const shell::SessionConfig config = shell::loadSessionConfig();
        wallpaper.setSettings(config.background);
        switcher.setStyle(config.switcher.style);
        desktop.setSettings(config);
        flip.setAnimationTimes(config.animations.flip, config.animations.flipStep);
    };
    tde::ConfigWatcher watcher({shell::sessionConfigPath()});
    QObject::connect(&watcher, &tde::ConfigWatcher::changed, applySettings);
    applySettings();

    // What this start was asked for, once running.
    if (request)
        bus.call(*request, QDBus::NoBlock);
    return QApplication::exec();
}
