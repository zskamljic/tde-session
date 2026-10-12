#include <Themes.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestThemes : public QObject {
    Q_OBJECT

private:
    static void write(const QString& path, const QByteArray& text = {})
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

    static QStringList ids(const std::vector<daedalus::Theme>& themes)
    {
        QStringList list;
        for (const auto& theme : themes)
            list << theme.id;
        return list;
    }

private slots:
    void findsIconAndPointerThemes()
    {
        QTemporaryDir dir;
        const QString user = dir.filePath(u"user"_s);
        const QString system = dir.filePath(u"system"_s);
        write(system + u"/Paper/index.theme"_s, "[Icon Theme]\nName=Paper Icons\nDirectories=16x16/apps\n");
        write(system + u"/Paper/cursors/default"_s);
        write(system + u"/hicolor/index.theme"_s, "[Icon Theme]\nName=Hicolor\nDirectories=16x16/apps\n");
        write(system + u"/Pointers/index.theme"_s, "[Icon Theme]\nName=Nice Pointers\n");
        write(system + u"/Pointers/cursors/default"_s);
        write(system + u"/Secret/index.theme"_s, "[Icon Theme]\nName=Secret\nDirectories=a\nHidden=true\n");
        // The user's own, of the same name, comes first.
        write(user + u"/Paper/index.theme"_s, "[Icon Theme]\nName=My Paper\nDirectories=16x16/apps\n");

        const auto icons = daedalus::iconThemes({user, system});
        QCOMPARE(ids(icons), QStringList {u"Paper"_s});
        QCOMPARE(icons.front().name, u"My Paper"_s);
        // Pointers of an icon theme are called by its folder, not by its icons' name.
        const auto cursors = daedalus::cursorThemes({system});
        QCOMPARE(
            cursors, (std::vector<daedalus::Theme> {{u"Pointers"_s, u"Nice Pointers"_s}, {u"Paper"_s, u"Paper"_s}}));
    }

    void findsGtkThemes()
    {
        QTemporaryDir dir;
        write(dir.filePath(u"Arc-Dark/gtk-3.0/gtk.css"_s));
        write(dir.filePath(u"OnlyGtk2/gtk-2.0/gtkrc"_s));
        QCOMPARE(
            ids(daedalus::gtkThemes({dir.path()})), (QStringList {u"Adwaita"_s, u"Adwaita-dark"_s, u"Arc-Dark"_s}));
    }
};

QTEST_MAIN(TestThemes)
#include "tst_themes.moc"
