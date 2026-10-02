#include <Applications.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace shell;

class TestApplications : public QObject {
    Q_OBJECT

private:
    void write(const QString& relative, const QByteArray& content)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    static QStringList ids(const std::vector<const Application*>& apps)
    {
        QStringList result;
        for (const Application* app : apps)
            result << app->id;
        return result;
    }

    static QStringList ids(const std::vector<Application>& apps)
    {
        QStringList result;
        for (const Application& app : apps)
            result << app.id;
        return result;
    }

    QTemporaryDir m_dir;
    std::vector<Application> m_apps;

private slots:
    void initTestCase()
    {
        qputenv("XDG_DATA_HOME", m_dir.filePath(u"home"_s).toLocal8Bit());
        qputenv("XDG_DATA_DIRS", m_dir.filePath(u"system"_s).toLocal8Bit());

        write(u"system/applications/org.gnome.TextEditor.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Text Editor\nName[de]=Texteditor\nGenericName=Editor\n"
            "Keywords=text;plaintext;write;\nExec=gnome-text-editor %U\nIcon=org.gnome.TextEditor\n");
        write(u"system/applications/ghostty.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Ghostty\nGenericName=Terminal Emulator\n"
            "Keywords=shell;prompt;command;commandline;\nExec=ghostty\n");
        write(u"system/applications/ariadne.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Files\nComment=Browse and organize your files\n"
            "Exec=ariadne %U\n");
        write(u"system/applications/cafe.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Café Timer\nExec=cafe --title=\"%c\" %F\nIcon=cafe\n"
            "StartupWMClass=Cafe-Timer\n");
        write(u"system/applications/htop.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=htop\nExec=htop\nTerminal=true\n");
        write(u"system/applications/helper.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Helper\nExec=helper\nNoDisplay=true\n");
        write(u"system/applications/kde-only.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=KDE Thing\nExec=kthing\nOnlyShowIn=KDE;\n");
        write(u"system/applications/not-here.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Not Here\nExec=nothere\nNotShowIn=GNOME;TDE;\n");
        write(u"system/applications/missing.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Missing\nExec=missing\nTryExec=no-such-program-anywhere\n");
        write(
            u"system/applications/link.desktop"_s, "[Desktop Entry]\nType=Link\nName=Link\nURL=https://example.org\n");
        write(
            u"system/applications/vendor/tool.desktop"_s, "[Desktop Entry]\nType=Application\nName=Tool\nExec=tool\n");
        // The user's own entries replace the system's, or hide them.
        write(u"home/applications/ghostty.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Ghostty\nGenericName=Terminal Emulator\n"
            "Exec=ghostty --gtk-single-instance=true\n");
        write(
            u"home/applications/vendor-tool.desktop"_s, "[Desktop Entry]\nType=Application\nName=Tool\nHidden=true\n");

        m_apps = loadApplications({u"TDE"_s});
    }

    void loadsWhatBelongsInMenus()
    {
        std::vector<Application> inMenus;
        std::ranges::copy_if(m_apps, std::back_inserter(inMenus), &Application::inMenus);
        QCOMPARE(ids(inMenus),
            (QStringList {u"cafe.desktop"_s, u"ariadne.desktop"_s, u"ghostty.desktop"_s, u"htop.desktop"_s,
                u"org.gnome.TextEditor.desktop"_s}));
        // Helpers stay known for their icons; what is not installed or not an app does not.
        QCOMPARE(ids(m_apps),
            (QStringList {u"cafe.desktop"_s, u"ariadne.desktop"_s, u"ghostty.desktop"_s, u"helper.desktop"_s,
                u"htop.desktop"_s, u"kde-only.desktop"_s, u"not-here.desktop"_s, u"org.gnome.TextEditor.desktop"_s}));
    }

    void findsTheAppOfAWindow_data()
    {
        QTest::addColumn<QString>("appId");
        QTest::addColumn<QString>("expected");
        QTest::newRow("file name") << u"org.gnome.TextEditor"_s << u"org.gnome.TextEditor.desktop"_s;
        QTest::newRow("case") << u"Ghostty"_s << u"ghostty.desktop"_s;
        QTest::newRow("wm class") << u"Cafe-Timer"_s << u"cafe.desktop"_s;
        QTest::newRow("last part") << u"com.example.htop"_s << u"htop.desktop"_s;
        QTest::newRow("program") << u"ariadne"_s << u"ariadne.desktop"_s;
        QTest::newRow("helper") << u"helper"_s << u"helper.desktop"_s;
        QTest::newRow("unknown") << u"nothing"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }

    void findsTheAppOfAWindow()
    {
        QFETCH(QString, appId);
        QFETCH(QString, expected);
        const Application* app = findApplication(m_apps, appId);
        QCOMPARE(app ? app->id : QString(), expected);
    }

    void userEntriesWin()
    {
        const auto ghostty = std::ranges::find(m_apps, u"ghostty.desktop"_s, &Application::id);
        QVERIFY(ghostty != m_apps.end());
        QCOMPARE(ghostty->exec, u"ghostty --gtk-single-instance=true"_s);
    }

    void searchRanksNamesFirst_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("name prefix") << u"tex"_s << QStringList {u"org.gnome.TextEditor.desktop"_s};
        QTest::newRow("word in name") << u"editor"_s << QStringList {u"org.gnome.TextEditor.desktop"_s};
        QTest::newRow("generic name") << u"terminal"_s << QStringList {u"ghostty.desktop"_s};
        QTest::newRow("keyword") << u"plain"_s << QStringList {u"org.gnome.TextEditor.desktop"_s};
        QTest::newRow("keyword of a replaced entry") << u"shell"_s << QStringList {};
        QTest::newRow("case and accents") << u"CAFE"_s << QStringList {u"cafe.desktop"_s};
        QTest::newRow("comment") << u"organize"_s << QStringList {u"ariadne.desktop"_s};
        QTest::newRow("program") << u"ariad"_s << QStringList {u"ariadne.desktop"_s};
        QTest::newRow("every word") << u"text write"_s << QStringList {u"org.gnome.TextEditor.desktop"_s};
        QTest::newRow("no match") << u"text shell"_s << QStringList {};
        QTest::newRow("empty") << u"  "_s << QStringList {};
    }

    void searchRanksNamesFirst()
    {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        QCOMPARE(ids(search(m_apps, query)), expected);
    }

    void betterMatchesComeFirst()
    {
        // "t": Text Editor and Café Timer by name, before Ghostty as a terminal.
        QCOMPARE(ids(search(m_apps, u"t"_s)),
            (QStringList {
                u"org.gnome.TextEditor.desktop"_s, u"cafe.desktop"_s, u"ghostty.desktop"_s, u"htop.desktop"_s}));
    }

    void expandsFieldCodes()
    {
        const auto cafe = std::ranges::find(m_apps, u"cafe.desktop"_s, &Application::id);
        QCOMPARE(commandLine(*cafe), (QStringList {u"cafe"_s, u"--title=Café Timer"_s}));
        Application withIcon;
        withIcon.name = u"X"_s;
        withIcon.exec = u"x %i %% \"two words\""_s;
        withIcon.icon = u"x"_s;
        QCOMPARE(commandLine(withIcon), (QStringList {u"x"_s, u"--icon"_s, u"x"_s, u"%"_s, u"two words"_s}));
    }

    void launchesInScopesAndTerminals()
    {
        const auto htop = std::ranges::find(m_apps, u"htop.desktop"_s, &Application::id);
        QCOMPARE(launchCommand(*htop, false), (QStringList {u"tde-session"_s, u"--terminal"_s, u"--"_s, u"htop"_s}));

        const auto editor = std::ranges::find(m_apps, u"org.gnome.TextEditor.desktop"_s, &Application::id);
        const QStringList command = launchCommand(*editor, true);
        QCOMPARE(command.first(), u"systemd-run"_s);
        QCOMPARE(command.last(), u"gnome-text-editor"_s);
        const QString unit = command.filter(u"--unit="_s).value(0);
        QVERIFY2(unit.startsWith(u"--unit=app-tde-org.gnome.TextEditor-"_s), qPrintable(unit));
        QVERIFY(unit.endsWith(u".scope"_s));
    }
};

QTEST_GUILESS_MAIN(TestApplications)
#include "tst_applications.moc"
