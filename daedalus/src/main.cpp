#include "SettingsWindow.hpp"

#include <tde/ConfigWatcher.hpp>
#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"tde-daedalus"_s);
    QApplication::setApplicationDisplayName(u"Settings"_s);
    QApplication::setApplicationVersion(QStringLiteral(TDE_SESSION_VERSION));
    QApplication::setDesktopFileName(u"tde-daedalus"_s);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"The settings of the TDE session."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption pageOption(u"page"_s,
        u"Open on the page called <name>: network, bluetooth, appearance, displays, sound, power, windows, "
        "keyboard, mouse, lock, apps or datetime."_s,
        u"name"_s);
    parser.addOption(pageOption);
    parser.process(app);
    const QString page = parser.value(pageOption);

    // One window: a second start shows the first on the page asked for.
    const QString service = u"io.github.zskamljic.Daedalus"_s;
    const QString path = u"/io/github/zskamljic/Daedalus"_s;
    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && !bus.registerService(service)) {
        QDBusMessage message = QDBusMessage::createMethodCall(service, path, service, u"ShowPage"_s);
        message << page;
        bus.call(message);
        return 0;
    }

    tde::createDesktopConfig(tde::desktopConfigPath());
    tde::setDesktop(tde::loadDesktopConfig());
    // The look of the parts settings pages are made of, after GNOME's.
    tde::theme::setApplicationStyleSheet(uR"(
QWidget#SidebarColumn { background: @sidebar@; }
QWidget#ContentColumn { background: @window@; }
QListWidget#Pages { background: transparent; border: none; outline: 0; padding: 6px; }
QListWidget#Pages::item { padding: 9px 8px; margin: 1px 0; border-radius: @radius_large@; color: @text@; }
QListWidget#Pages::item:hover { background: @hover@; }
QListWidget#Pages::item:selected { background: @pressed@; color: @text@; }
QWidget#BoxedList { background: @base@; border: 1px solid @border@; border-radius: 12px; }
QWidget#BoxedList QFrame#Separator { background: @border@; border: none; }
QLabel#Keys { color: @dim_text@; border: 1px solid @border@; border-radius: 6px; padding: 2px 8px; }
)"_s);
    tde::theme::apply(app, tde::desktop().appearance);

    daedalus::SettingsWindow window;
    if (!page.isEmpty() && !window.showPage(page))
        qWarning("tde-daedalus: there is no page called %s", qPrintable(page));
    bus.registerObject(path, &window, QDBusConnection::ExportScriptableSlots);
    window.show();
    return QApplication::exec();
}
