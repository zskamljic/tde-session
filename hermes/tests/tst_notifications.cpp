#include "Notifications.hpp"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace hermes;

class TestNotifications : public QObject {
    Q_OBJECT

private:
    static uint notify(NotificationServer& server, const QString& summary, uint replaces = 0,
        const QStringList& actions = {}, const QVariantMap& hints = {}, int timeout = -1)
    {
        return server.Notify(u"Test"_s, replaces, {}, summary, {}, actions, hints, timeout);
    }

private slots:
    void newestFirstAndReplaced()
    {
        NotificationServer server;
        QSignalSpy arrived(&server, &NotificationServer::arrived);
        QSignalSpy replaced(&server, &NotificationServer::replaced);
        const uint first = notify(server, u"one"_s);
        const uint second = notify(server, u"two"_s);
        QVERIFY(first != 0 && second != first);
        QCOMPARE(server.notifications().front().summary, u"two"_s);

        // A replacement keeps its id and place, and shows its banner again.
        server.expire(first);
        QVERIFY(!server.find(first)->banner);
        QCOMPARE(notify(server, u"one again"_s, first), first);
        QCOMPARE(server.notifications().size(), size_t(2));
        QCOMPARE(server.find(first)->summary, u"one again"_s);
        QVERIFY(server.find(first)->banner);
        QCOMPARE(arrived.count(), 2);
        QCOMPARE(replaced.count(), 1);
    }

    void expiredStayUnlessTransient()
    {
        NotificationServer server;
        QSignalSpy closed(&server, &NotificationServer::NotificationClosed);
        const uint kept = notify(server, u"kept"_s);
        const uint transient = notify(server, u"gone"_s, 0, {}, {{u"transient"_s, true}});
        server.expire(kept);
        server.expire(transient);
        QVERIFY(server.find(kept));
        QVERIFY(!server.find(transient));
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationServer::Reason::Expired));
    }

    void actionsCloseUnlessResident()
    {
        NotificationServer server;
        QSignalSpy invoked(&server, &NotificationServer::ActionInvoked);
        QSignalSpy closed(&server, &NotificationServer::NotificationClosed);
        const QStringList actions {u"default"_s, u"Open"_s, u"reply"_s, u"Reply"_s};
        const uint plain = notify(server, u"plain"_s, 0, actions);
        const uint resident = notify(server, u"resident"_s, 0, actions, {{u"resident"_s, true}});
        QVERIFY(server.find(plain)->hasDefaultAction());

        server.invoke(plain, u"reply"_s);
        server.invoke(resident, u"default"_s);
        QCOMPARE(invoked.count(), 2);
        QCOMPARE(invoked.at(0).at(1).toString(), u"reply"_s);
        QVERIFY(!server.find(plain));
        QVERIFY(server.find(resident));
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationServer::Reason::Dismissed));
    }

    void closedByTheProgram()
    {
        NotificationServer server;
        QSignalSpy closed(&server, &NotificationServer::NotificationClosed);
        const uint id = notify(server, u"x"_s);
        server.CloseNotification(id);
        server.CloseNotification(id); // already gone: nothing more
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationServer::Reason::Closed));
    }

    void clearingLeavesBanners()
    {
        NotificationServer server;
        const uint old = notify(server, u"old"_s);
        const uint fresh = notify(server, u"fresh"_s);
        server.expire(old);
        server.dismissAll();
        QVERIFY(!server.find(old));
        QVERIFY(server.find(fresh));
    }

    void tokenBeforeAction()
    {
        NotificationServer server;
        QStringList order;
        connect(&server, &NotificationServer::ActivationToken, this,
            [&](uint, const QString& token) { order << u"token "_s + token; });
        connect(&server, &NotificationServer::ActionInvoked, this,
            [&](uint, const QString& action) { order << u"action "_s + action; });
        server.invoke(notify(server, u"x"_s, 0, {u"default"_s, u"Open"_s}), u"default"_s, u"abc"_s);
        server.invoke(notify(server, u"y"_s, 0, {u"default"_s, u"Open"_s}), u"default"_s);
        QCOMPARE(order, (QStringList {u"token abc"_s, u"action default"_s, u"action default"_s}));
    }

    void sounds()
    {
        NotificationServer server;
        const auto event
            = [&](const QVariantMap& hints) { return server.find(notify(server, u"s"_s, 0, {}, hints))->sound(); };
        QCOMPARE(event({}), u"message-new-instant"_s);
        QCOMPARE(event({{u"urgency"_s, QVariant::fromValue(uchar(2))}}), u"dialog-warning"_s);
        QCOMPARE(event({{u"urgency"_s, QVariant::fromValue(uchar(0))}}), QString());
        QCOMPARE(event({{u"sound-name"_s, u"bell"_s}}), u"bell"_s);
        QCOMPARE(event({{u"suppress-sound"_s, true}}), QString());
        QCOMPARE(event({{u"sound-file"_s, u"file:///tmp/ding.oga"_s}}), u"/tmp/ding.oga"_s);
        QCOMPARE(event({{u"sound-file"_s, u"/tmp/ding.oga"_s}, {u"suppress-sound"_s, true}}), QString());

        // Only what is new sounds, not a notification updating itself.
        QSignalSpy arrived(&server, &NotificationServer::arrived);
        const uint id = notify(server, u"progress 1"_s);
        notify(server, u"progress 2"_s, id);
        QCOMPARE(arrived.count(), 1);
    }

    void markup_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<qsizetype>("maxLength");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << u"Tom & Jerry\n<3"_s << qsizetype(-1) << u"Tom &amp; Jerry<br>&lt;3"_s;
        QTest::newRow("entities") << u"a &amp; b &#169;"_s << qsizetype(-1) << u"a &amp; b &#169;"_s;
        QTest::newRow("kept") << u"<b>bold</b> <I>it</I> <u>u</u>"_s << qsizetype(-1)
                              << u"<b>bold</b> <i>it</i> <u>u</u>"_s;
        QTest::newRow("dropped")
            << u"<table><tr><td style=\"x\">cell</td></tr></table><img src=\"file:///etc/passwd\"/>"_s << qsizetype(-1)
            << u"cell"_s;
        QTest::newRow("web link") << u"<a href=\"https://example.org/?a=1&b=2\">site</a>"_s << qsizetype(-1)
                                  << u"<a href=\"https://example.org/?a=1&amp;b=2\">site</a>"_s;
        QTest::newRow("other link") << u"<a href='file:///home'>home</a>"_s << qsizetype(-1) << u"home"_s;
        QTest::newRow("unclosed") << u"<b>open <i>both"_s << qsizetype(-1) << u"<b>open <i>both</i></b>"_s;
        QTest::newRow("stray close") << u"</b>text</i>"_s << qsizetype(-1) << u"text"_s;
        QTest::newRow("cut in text") << u"<b>abcdef</b>ghi"_s << qsizetype(4) << u"<b>abcd…</b>"_s;
        QTest::newRow("only space after cut") << u"hello<b></b>  "_s << qsizetype(5) << u"hello<b></b>"_s;
        QTest::newRow("deep nesting") << u"<b><b><b><b><b><b><b><b><b><b><b><b><b><b><b><b><b><b>x"_s << qsizetype(-1)
                                      << QString(u"<b>"_s.repeated(16) + u"x"_s + u"</b>"_s.repeated(16));
        QTest::newRow("cut before tag") << u"abcd<a href=\"https://x.org\">link</a>"_s << qsizetype(4) << u"abcd…"_s;
    }

    void markup()
    {
        QFETCH(QString, body);
        QFETCH(qsizetype, maxLength);
        QFETCH(QString, expected);
        QCOMPARE(bodyMarkup(body, maxLength), expected);
    }

    void hostileBodiesAreQuick()
    {
        // Tags that never close, closing tags never opened: a body any program could send to
        // keep the bar busy. It is bounded, and nested only so deep.
        const QString body = u"<b>"_s.repeated(40'000) + u"</i>"_s.repeated(40'000);
        QElapsedTimer timer;
        timer.start();
        const QString markup = bodyMarkup(body);
        QVERIFY2(timer.elapsed() < 1000, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
        QCOMPARE(markup.count(u"<b>"_s), 16);
    }

    void bannerTimes()
    {
        NotificationServer server;
        QCOMPARE(server.find(notify(server, u"a"_s))->bannerTime(), 5000);
        QCOMPARE(server.find(notify(server, u"b"_s, 0, {}, {}, 1200))->bannerTime(), 1200);
        QCOMPARE(server.find(notify(server, u"c"_s, 0, {}, {}, 0))->bannerTime(), 0);
        const uint critical = notify(server, u"d"_s, 0, {}, {{u"urgency"_s, QVariant::fromValue(uchar(2))}});
        QCOMPARE(server.find(critical)->urgency, Notification::Urgency::Critical);
        QCOMPARE(server.find(critical)->bannerTime(), 0);
    }
};

QTEST_GUILESS_MAIN(TestNotifications)
#include "tst_notifications.moc"
