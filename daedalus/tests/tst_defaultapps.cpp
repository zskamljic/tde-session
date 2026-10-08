#include <DefaultAppsPage.hpp>

#include <QTest>

using namespace Qt::StringLiterals;

class TestDefaultApps : public QObject {
    Q_OBJECT

private slots:
    void readsTheDefault()
    {
        const QString list = u"[Added Associations]\n"
                             "text/html=other.desktop;\n"
                             "\n"
                             "[Default Applications]\n"
                             "text/html = firefox.desktop;chromium.desktop;\n"_s;
        QCOMPARE(daedalus::defaultIn(list, u"text/html"_s), u"firefox.desktop"_s);
        QVERIFY(daedalus::defaultIn(list, u"inode/directory"_s).isEmpty());
    }

    void replacesAndAddsKeepingTheRest()
    {
        const QString list = u"[Default Applications]\n"
                             "text/html=firefox.desktop;\n"
                             "\n"
                             "[Added Associations]\n"
                             "image/png=gimp.desktop;\n"_s;
        QCOMPARE(daedalus::withDefault(list, {u"text/html"_s, u"x-scheme-handler/http"_s}, u"chromium.desktop"_s),
            u"[Default Applications]\n"
            "text/html=chromium.desktop;\n"
            "x-scheme-handler/http=chromium.desktop;\n"
            "\n"
            "[Added Associations]\n"
            "image/png=gimp.desktop;\n"_s);
    }

    void startsTheSectionWhenThereIsNone()
    {
        QCOMPARE(daedalus::withDefault({}, {u"inode/directory"_s}, u"ariadne.desktop"_s),
            u"[Default Applications]\ninode/directory=ariadne.desktop;\n"_s);
        QCOMPARE(daedalus::withDefault(
                     u"[Added Associations]\na/b=c.desktop;\n"_s, {u"inode/directory"_s}, u"ariadne.desktop"_s),
            u"[Added Associations]\na/b=c.desktop;\n\n[Default Applications]\ninode/directory=ariadne.desktop;\n"_s);
    }
};

QTEST_GUILESS_MAIN(TestDefaultApps)
#include "tst_defaultapps.moc"
