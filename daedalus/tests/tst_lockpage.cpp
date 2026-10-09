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

    void setsTopLevelValues()
    {
        QTemporaryDir dir;
        const QString path = write(dir,
            "return {\n    -- terminal = \"kitty\",\n    appearance = { terminal = \"x\" },\n}\n");
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
