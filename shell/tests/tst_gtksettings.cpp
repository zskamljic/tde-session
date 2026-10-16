#include <GtkSettings.hpp>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QString read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void write(const QString& path, const QByteArray& text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

} // namespace

class TestGtkSettings : public QObject {
    Q_OBJECT

private slots:
    void missingFileIsMade()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"gtk-3.0/settings.ini"_s);
        QVERIFY(shell::setGtkSetting(path, u"gtk-decoration-layout"_s, u"close,minimize,maximize:"_s));
        QCOMPARE(read(path), u"[Settings]\ngtk-decoration-layout=close,minimize,maximize:\n"_s);
    }

    void addedToTheGroupKeepingTheRest()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        write(path, "# mine\n[Settings]\ngtk-theme-name=Adwaita\n\n[Other]\na=b\n");
        QVERIFY(shell::setGtkSetting(path, u"gtk-decoration-layout"_s, u":close"_s));
        QCOMPARE(read(path),
            u"# mine\n[Settings]\ngtk-theme-name=Adwaita\ngtk-decoration-layout=:close\n\n[Other]\na=b\n"_s);
    }

    void replacedWhereItIs()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        write(path, "[Settings]\ngtk-decoration-layout = menu:close\ngtk-theme-name=Adwaita\n");
        QVERIFY(shell::setGtkSetting(path, u"gtk-decoration-layout"_s, u"close:"_s));
        QCOMPARE(read(path), u"[Settings]\ngtk-decoration-layout=close:\ngtk-theme-name=Adwaita\n"_s);
    }

    void groupAddedWhenOnlyOthersAreThere()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        write(path, "[Other]\ngtk-decoration-layout=x\n");
        QVERIFY(shell::setGtkSetting(path, u"gtk-decoration-layout"_s, u"close:"_s));
        QCOMPARE(read(path), u"[Other]\ngtk-decoration-layout=x\n[Settings]\ngtk-decoration-layout=close:\n"_s);
    }

    void bothVersions()
    {
        QTemporaryDir dir;
        QVERIFY(shell::setGtkSettings(dir.path(), u"gtk-decoration-layout"_s, u"close:"_s));
        for (const char* gtk : {"gtk-3.0", "gtk-4.0"})
            QVERIFY(read(dir.filePath(QLatin1StringView(gtk) + u"/settings.ini"_s)).contains(u"=close:"_s));
    }
};

QTEST_GUILESS_MAIN(TestGtkSettings)
#include "tst_gtksettings.moc"
