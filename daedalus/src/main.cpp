#include "SettingsWindow.hpp"

#include <tde/ConfigWatcher.hpp>
#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QApplication>
#include <QCommandLineParser>

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
    parser.process(app);

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
    window.show();
    return QApplication::exec();
}
