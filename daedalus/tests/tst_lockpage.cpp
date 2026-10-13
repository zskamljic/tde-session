#include <DesktopSettings.hpp>
#include <LockPage.hpp>

#include <tde/DesktopConfig.hpp>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestLockPage : public QObject {
    Q_OBJECT

private:
    static QString write(const QTemporaryDir& dir, const QByteArray& text)
    {
        const QString path = dir.filePath(u"config.lua"_s);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(text);
        return path;
    }

    static int lockAfter(const QString& path)
    {
        tde::DesktopConfig config;
        const auto result = tde::readDesktopConfig(path, config);
        return result ? config.lock.after : -1;
    }

    static QString read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

private slots:
    void changesOnlyTheValue()
    {
        QTemporaryDir dir;
        const QString path = write(dir,
            "-- mine\nreturn {\n    appearance = { theme = \"arc\" },\n    lock = {\n        -- how long\n"
            "        after = 5, -- minutes\n    },\n}\n");
        QVERIFY(daedalus::setLockAfter(path, 0));
        QCOMPARE(lockAfter(path), 0);
        QVERIFY(daedalus::setLockAfter(path, 12));
        QCOMPARE(lockAfter(path), 12);
        QCOMPARE(read(path),
            u"-- mine\nreturn {\n    appearance = { theme = \"arc\" },\n    lock = {\n        -- how long\n"
            "        after = 12, -- minutes\n    },\n}\n"_s);
    }

    void skipsNestedTablesAndComments()
    {
        QTemporaryDir dir;
        const QString path = write(dir,
            "return {\n    appearance = {\n        -- theme = \"system\",\n        colors = { theme = \"#fff\" },\n"
            "        theme = 'arc', -- light\n        corner_radius = 5,\n    },\n    lock = { after = 5 },\n}\n");
        QVERIFY(shell::setDesktopString(path, u"appearance"_s, u"theme"_s, u"arc-dark"_s));
        QVERIFY(daedalus::setLockAfter(path, 9));
        QCOMPARE(read(path),
            u"return {\n    appearance = {\n        -- theme = \"system\",\n        colors = { theme = \"#fff\" },\n"
            "        theme = \"arc-dark\", -- light\n        corner_radius = 5,\n    },\n    lock = { after = 9 },\n}\n"_s);
        tde::DesktopConfig config;
        QVERIFY(tde::readDesktopConfig(path, config));
        QCOMPARE(config.appearance.theme, u"arc-dark"_s);
    }

    void addsTheTableWhenMissing()
    {
        // As older programs wrote it: no lock table at all.
        QTemporaryDir dir;
        const QString path = write(dir,
            "-- TDE desktop configuration, shared by all TDE applications.\n"
            "-- See /usr/share/doc/ariadne/examples/tde/config.lua (or data/tde/config.lua in the Ariadne sources) "
            "for every setting.\n\n"
            "return {\n    terminal = \"ghostty\",\n\n    window_buttons = {\n        position = \"left\",\n"
            "        order = { \"close\", \"minimize\", \"maximize\" },\n    },\n\n    appearance = {\n"
            "        theme = \"arc-dark\",\n        corner_radius = 5,\n        icon_theme = \"Paper\",\n    },\n}\n");
        QVERIFY(daedalus::setLockAfter(path, 0));
        QCOMPARE(lockAfter(path), 0);
        QVERIFY(shell::setDesktopSetting(path, u"lock"_s, u"before_sleep"_s, u"false"_s));
        QCOMPARE(lockAfter(path), 0);
        QVERIFY(daedalus::setLockAfter(path, 10));
        QCOMPARE(lockAfter(path), 10);
    }

    void addsACommaWhereTheLastEntryHasNone()
    {
        QTemporaryDir dir;
        const QString path = write(dir, "return {\n    appearance = { theme = \"arc\" } -- mine\n}\n");
        QVERIFY(daedalus::setLockAfter(path, 0));
        QCOMPARE(lockAfter(path), 0);
        QCOMPARE(
            read(path), u"return {\n    appearance = { theme = \"arc\" }, -- mine\n    lock = { after = 0 },\n}\n"_s);

        const QString text = write(dir, "return {\n    terminal = \"foot\"\n}\n");
        QVERIFY(shell::setDesktopString(text, u"appearance"_s, u"theme"_s, u"arc"_s));
        QCOMPARE(read(text), u"return {\n    terminal = \"foot\",\n    appearance = { theme = \"arc\" },\n}\n"_s);
    }

    void repairsWhatOlderVersionsBroke()
    {
        // Before 0.3.0, a setting added after a last entry without a comma left the file
        // unreadable, and every edit after went into it unseen.
        QTemporaryDir dir;
        const QString path = write(dir,
            "return {\n    window_buttons = { position = \"left\" },\n    appearance = {\n"
            "        theme = \"arc\" -- light\n    }\n    lock = { after = 0 },\n    terminal = \"foot\"\n"
            "    lock = { before_sleep = false, after = 3 },\n}\n");
        QVERIFY(!shell::desktopConfigError(path).isEmpty());
        QVERIFY(shell::repairDesktopConfig(path));
        QVERIFY(shell::desktopConfigError(path).isEmpty());
        tde::DesktopConfig config;
        QVERIFY(tde::readDesktopConfig(path, config));
        QCOMPARE(config.windowButtons.side, tde::ButtonSide::Left);
        QCOMPARE(config.lock.after, 3);
        QVERIFY(!config.lock.beforeSleep);
        QCOMPARE(read(path),
            u"return {\n    window_buttons = { position = \"left\" },\n    appearance = {\n"
            "        theme = \"arc\" -- light\n    },\n    lock = { after = 0 },\n    terminal = \"foot\",\n"
            "    lock = { before_sleep = false, after = 3 },\n}\n"_s);
        // Readable already: left as it is.
        QVERIFY(!shell::repairDesktopConfig(path));
        // The lock table given twice: the last one is what counts, and what changes.
        QVERIFY(daedalus::setLockAfter(path, 15));
        QCOMPARE(lockAfter(path), 15);

        // Broken otherwise: left for its owner to fix.
        const QString other = write(dir, "return { appearance = { theme = } }\n");
        QVERIFY(!shell::repairDesktopConfig(other));
        QCOMPARE(read(other), u"return { appearance = { theme = } }\n"_s);
    }

    void setsTopLevelValues()
    {
        QTemporaryDir dir;
        const QString path
            = write(dir, "return {\n    -- terminal = \"kitty\",\n    appearance = { terminal = \"x\" },\n}\n");
        QVERIFY(shell::setDesktopSetting(path, {}, u"terminal"_s, u"\"foot\""_s));
        QVERIFY(shell::setDesktopSetting(path, {}, u"terminal"_s, u"\"ghostty\""_s));
        QCOMPARE(read(path),
            u"return {\n    -- terminal = \"kitty\",\n    appearance = { terminal = \"x\" },\n    terminal = \"ghostty\",\n}\n"_s);
    }

    void addsWhatIsMissing()
    {
        QTemporaryDir dir;
        const QString withoutLock = write(dir, "return {\n    terminal = \"kitty\",\n}\n");
        QVERIFY(daedalus::setLockAfter(withoutLock, 7));
        QCOMPARE(lockAfter(withoutLock), 7);

        const QString emptyLock = write(dir, "return { lock = {} }\n");
        QVERIFY(daedalus::setLockAfter(emptyLock, 3));
        QCOMPARE(lockAfter(emptyLock), 3);
    }
};

QTEST_GUILESS_MAIN(TestLockPage)
#include "tst_lockpage.moc"
