#include "DBusMenu.hpp"

#include <QMenu>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace hermes;

class TestDBusMenu : public QObject {
    Q_OBJECT

private slots:
    void labels_data()
    {
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("expected");
        QTest::newRow("mnemonic") << u"_Quit"_s << u"&Quit"_s;
        QTest::newRow("underscore") << u"snake__case"_s << u"snake_case"_s;
        QTest::newRow("ampersand") << u"Tom & Jerry"_s << u"Tom && Jerry"_s;
        QTest::newRow("plain") << u"Show"_s << u"Show"_s;
    }

    void labels()
    {
        QFETCH(QString, label);
        QFETCH(QString, expected);
        QCOMPARE(menuLabel(label), expected);
    }

    void fillsMenus()
    {
        MenuNode root;
        root.children = {
            {1, {{u"label"_s, u"_Show"_s}}, {}},
            {2, {{u"type"_s, u"separator"_s}}, {}},
            {3, {{u"label"_s, u"Hidden"_s}, {u"visible"_s, false}}, {}},
            {4, {{u"label"_s, u"Mute"_s}, {u"toggle-type"_s, u"checkmark"_s}, {u"toggle-state"_s, 1}}, {}},
            {5, {{u"label"_s, u"More"_s}, {u"children-display"_s, u"submenu"_s}},
                {{6, {{u"label"_s, u"Off"_s}, {u"enabled"_s, false}}, {}}}},
        };
        QMenu menu;
        fillMenu(&menu, root, u"org.example"_s, u"/Menu"_s);
        const auto actions = menu.actions();
        QCOMPARE(actions.size(), 4);
        QCOMPARE(actions[0]->text(), u"&Show"_s);
        QVERIFY(actions[1]->isSeparator());
        QVERIFY(actions[2]->isCheckable() && actions[2]->isChecked());
        QVERIFY(actions[3]->menu());
        QCOMPARE(actions[3]->menu()->actions().size(), 1);
        QVERIFY(!actions[3]->menu()->actions()[0]->isEnabled());
    }
};

QTEST_MAIN(TestDBusMenu)
#include "tst_dbusmenu.moc"
