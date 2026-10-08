#include <KeyboardPage.hpp>

#include <QTest>

using namespace Qt::StringLiterals;
using daedalus::InputSource;

class TestKeyboard : public QObject {
    Q_OBJECT

private slots:
    void readsLayoutsAndVariants()
    {
        const auto sources = daedalus::parseXkbList(u"! model\n"
                                                    "  pc105           Generic 105-key PC\n"
                                                    "\n"
                                                    "! layout\n"
                                                    "  us              English (US)\n"
                                                    "  si              Slovenian\n"
                                                    "\n"
                                                    "! variant\n"
                                                    "  dvorak          us: English (Dvorak)\n"
                                                    "  us              si: Slovenian (US)\n"
                                                    "\n"
                                                    "! option\n"
                                                    "  grp             Switching to another layout\n"_s);
        const std::vector<InputSource> expected {
            {u"us"_s, u"dvorak"_s, u"English (Dvorak)"_s},
            {u"us"_s, {}, u"English (US)"_s},
            {u"si"_s, {}, u"Slovenian"_s},
            {u"si"_s, u"us"_s, u"Slovenian (US)"_s},
        };
        QCOMPARE(sources, expected);
    }

    void pairsLayoutsWithVariants()
    {
        const auto sources = daedalus::sourcesOf(u"us,si"_s, u"dvorak,"_s);
        QCOMPARE(sources.size(), 2u);
        QCOMPARE(sources[0].variant, u"dvorak"_s);
        QCOMPARE(sources[1].layout, u"si"_s);
        QVERIFY(sources[1].variant.isEmpty());
        QCOMPARE(daedalus::layoutsOf(sources), std::pair(u"us,si"_s, u"dvorak,"_s));
    }

    void leavesOutEmptyVariants()
    {
        QCOMPARE(daedalus::layoutsOf(daedalus::sourcesOf(u"us,si"_s, {})), std::pair(u"us,si"_s, QString()));
    }

    void replacesTheSwitchingOption()
    {
        QCOMPARE(daedalus::switchOption(u"compose:ralt,grp:alt_shift_toggle"_s), u"grp:alt_shift_toggle"_s);
        QVERIFY(daedalus::switchOption(u"compose:ralt"_s).isEmpty());
        QCOMPARE(daedalus::withSwitchOption(u"grp:alt_shift_toggle,compose:ralt"_s, u"grp:win_space_toggle"_s),
            u"compose:ralt,grp:win_space_toggle"_s);
        QCOMPARE(daedalus::withSwitchOption(u"grp:alt_shift_toggle"_s, {}), QString());
    }
};

QTEST_GUILESS_MAIN(TestKeyboard)
#include "tst_keyboard.moc"
