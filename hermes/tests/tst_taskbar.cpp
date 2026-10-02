#include "Groups.hpp"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace hermes;

namespace {

shell::Application app(const QString& id, const QString& exec)
{
    shell::Application result;
    result.id = id;
    result.name = id.section(u'.', -2, -2);
    result.exec = exec;
    return result;
}

QStringList keys(const std::vector<Group>& groups)
{
    QStringList result;
    for (const Group& group : groups)
        result << group.key;
    return result;
}

} // namespace

class TestTaskbar : public QObject {
    Q_OBJECT

private:
    std::vector<shell::Application> m_apps {
        app(u"ariadne.desktop"_s, u"ariadne %U"_s),
        app(u"com.mitchellh.ghostty.desktop"_s, u"ghostty"_s),
        app(u"org.gnome.TextEditor.desktop"_s, u"gnome-text-editor %U"_s),
    };

private slots:
    void groupedByApplicationInOpeningOrder()
    {
        const std::vector<OpenWindow> windows {
            {1, u"org.gnome.TextEditor"_s},
            {2, u"xterm"_s},
            {3, u"com.mitchellh.ghostty"_s},
            {4, u"org.gnome.TextEditor"_s},
            {5, {}},
        };
        const auto groups = groupWindows(windows, m_apps);
        QCOMPARE(keys(groups),
            (QStringList {u"org.gnome.TextEditor.desktop"_s, u"xterm"_s, u"com.mitchellh.ghostty.desktop"_s, u"#5"_s}));
        QCOMPARE(groups[0].windows, (std::vector<quint64> {1, 4}));
        QVERIFY(groups[1].app == nullptr);
        QCOMPARE(groups[2].windows, std::vector<quint64> {3});
    }

    void windowsOfNotifications()
    {
        QVERIFY(isWindowOf(u"org.gnome.TextEditor"_s, u"org.gnome.TextEditor"_s, u"Text Editor"_s));
        QVERIFY(isWindowOf(u"org.gnome.TextEditor"_s, {}, u"Text Editor"_s));
        QVERIFY(isWindowOf(u"firefox"_s, u"org.mozilla.firefox.desktop"_s, {}));
        QVERIFY(isWindowOf(u"firefox"_s, {}, u"Firefox"_s));
        QVERIFY(isWindowOf(u"com.mitchellh.ghostty"_s, {}, u"ghostty"_s));
        QVERIFY(!isWindowOf(u"com.mitchellh.ghostty"_s, {}, u"notify-send"_s));
        QVERIFY(!isWindowOf({}, {}, {}));
    }

    void clickSwitchesAndMinimizes()
    {
        const Group group {.key = u"g"_s, .app = &m_apps[0], .windows = {7}};
        std::vector<OpenWindow> windows {{7, u"ariadne"_s, false, false}};
        QCOMPARE(click(group, windows, {7}).action, Click::Activate);
        windows[0].activated = true;
        QCOMPARE(click(group, windows, {7}).action, Click::Minimize);
        // Minimized windows keep the activated state in some compositors; they come back.
        windows[0].minimized = true;
        QCOMPARE(click(group, windows, {7}).action, Click::Activate);
    }

    void clickCyclesThroughSeveralWindows()
    {
        const Group group {.key = u"g"_s, .app = &m_apps[0], .windows = {1, 2, 3}};
        std::vector<OpenWindow> windows {{1, {}}, {2, {}}, {3, {}}};
        // None active: the one used last.
        Click result = click(group, windows, {9, 2, 1, 3});
        QCOMPARE(result.action, Click::Activate);
        QCOMPARE(result.window, 2u);
        // Active: the next one, round again after the last.
        windows[1].activated = true;
        QCOMPARE(click(group, windows, {2}).window, 3u);
        windows[1].activated = false;
        windows[2].activated = true;
        QCOMPARE(click(group, windows, {3}).window, 1u);
    }
};

QTEST_GUILESS_MAIN(TestTaskbar)
#include "tst_taskbar.moc"
