#include <DisplaysPage.hpp>

#include <QTest>

class TestArrangement : public QObject {
    Q_OBJECT

private slots:
    void droppedBesideItStaysThere()
    {
        const QRect laptop(0, 0, 1920, 1200);
        QCOMPARE(daedalus::Arrangement::snap(QRect(1920, 300, 2560, 1440), {laptop}), QPoint(1920, 300));
    }

    void droppedOverlappingMovesOut()
    {
        const QRect laptop(0, 0, 1920, 1200);
        // Mostly to the right of it: it goes there, as high as it was.
        QCOMPARE(daedalus::Arrangement::snap(QRect(1500, 400, 1920, 1080), {laptop}), QPoint(1920, 400));
        // Mostly below it: it goes under it.
        QCOMPARE(daedalus::Arrangement::snap(QRect(200, 1000, 1920, 1080), {laptop}), QPoint(200, 1200));
    }

    void nearlyLinedUpLinesUp()
    {
        const QRect laptop(0, 0, 1920, 1200);
        QCOMPARE(daedalus::Arrangement::snap(QRect(40, 1200, 1280, 720), {laptop}), QPoint(0, 1200));
        QCOMPARE(daedalus::Arrangement::snap(QRect(1920, 30, 1280, 720), {laptop}), QPoint(1920, 0));
    }

    void middlesLineUpToo()
    {
        const QRect laptop(0, 0, 1920, 1200);
        QCOMPARE(daedalus::Arrangement::snap(QRect(1920, 230, 1280, 720), {laptop}, 100), QPoint(1920, 240));
        QCOMPARE(daedalus::Arrangement::snap(QRect(1920, 60, 1280, 720), {laptop}, 100), QPoint(1920, 0));
        // Further than the pull, it stays where it was let go.
        QCOMPARE(daedalus::Arrangement::snap(QRect(1920, 120, 1280, 720), {laptop}, 100), QPoint(1920, 120));
    }

    void droppedFarAwayComesBack()
    {
        const QRect laptop(0, 0, 1920, 1200);
        QCOMPARE(daedalus::Arrangement::snap(QRect(5000, 0, 1920, 1080), {laptop}), QPoint(1920, 0));
    }

    void neverCoversAnother()
    {
        const std::vector<QRect> others {QRect(0, 0, 1920, 1080), QRect(1920, 0, 1920, 1080)};
        const QPoint place = daedalus::Arrangement::snap(QRect(1000, 500, 1920, 1080), others);
        const QRect area(place, QSize(1920, 1080));
        for (const QRect& other : others)
            QVERIFY(!area.intersects(other));
    }
};

QTEST_GUILESS_MAIN(TestArrangement)
#include "tst_arrangement.moc"
