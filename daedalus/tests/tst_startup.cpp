#include <StartupPage.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestStartup : public QObject {
    Q_OBJECT

private:
    static void write(const QString& path, const QByteArray& text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

    static QString read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

private slots:
    void setsTheKeysInTheEntryGroup()
    {
        const QString text = u"[Desktop Entry]\nName=Thing\nHidden=true\nExec=thing\n"
                             "X-GNOME-Autostart-enabled=false\n\n[Desktop Action new]\nHidden=true\n"_s;
        QCOMPARE(daedalus::withAutostartEnabled(text, true),
            u"[Desktop Entry]\nHidden=false\nX-GNOME-Autostart-enabled=true\nName=Thing\nExec=thing\n\n"
            "[Desktop Action new]\nHidden=true\n"_s);
        QCOMPARE(daedalus::withAutostartEnabled(u"[Desktop Entry]\nName=Thing\n"_s, false),
            u"[Desktop Entry]\nHidden=true\nX-GNOME-Autostart-enabled=false\nName=Thing\n"_s);
    }

    void listsTheUsersOverTheSystems()
    {
        QTemporaryDir dir;
        const QString user = dir.filePath(u"user"_s);
        const QString system = dir.filePath(u"system"_s);
        write(system + u"/agent.desktop"_s, "[Desktop Entry]\nType=Application\nName=Agent\nExec=agent\n");
        write(system + u"/gnome-only.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Gnome\nExec=g\nOnlyShowIn=GNOME;\n");
        write(system + u"/hidden-part.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Part\nExec=p\nNoDisplay=true\n");
        write(user + u"/mine.desktop"_s, "[Desktop Entry]\nType=Application\nName=Mine\nExec=mine\n");

        auto entries = shell::autostartEntries({user, system}, {u"TDE"_s});
        QCOMPARE(entries.size(), 2u);
        QCOMPARE(entries[0].name, u"Agent"_s);
        QVERIFY(entries[0].enabled && entries[0].system && !entries[0].user);
        QCOMPARE(entries[1].name, u"Mine"_s);
        QVERIFY(entries[1].enabled && !entries[1].system && entries[1].user);

        // Turned off: by a copy of the user's, which the system's stays behind.
        QVERIFY(daedalus::setAutostartEnabled(entries[0], false, user));
        QVERIFY(QFile::exists(system + u"/agent.desktop"_s));
        entries = shell::autostartEntries({user, system}, {u"TDE"_s});
        QCOMPARE(entries[0].path, user + u"/agent.desktop"_s);
        QVERIFY(!entries[0].enabled && entries[0].system && entries[0].user);
        QVERIFY(read(entries[0].path).contains(u"Exec=agent"_s));

        QVERIFY(daedalus::setAutostartEnabled(entries[0], true, user));
        entries = shell::autostartEntries({user, system}, {u"TDE"_s});
        QVERIFY(entries[0].enabled);
    }
};

QTEST_MAIN(TestStartup)
#include "tst_startup.moc"
