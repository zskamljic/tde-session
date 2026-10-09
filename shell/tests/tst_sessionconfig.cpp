#include <SessionConfig.hpp>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestSessionConfig : public QObject {
    Q_OBJECT

private slots:
    void missingFileKeepsDefaults()
    {
        QTemporaryDir dir;
        QCOMPARE(shell::loadSessionConfig(dir.filePath(u"none.lua"_s)), shell::SessionConfig());
    }

    void savedSettingsLoadAgain()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"session/config.lua"_s);
        shell::SessionConfig config;
        config.background.image = u"/home/me/Pictures/\"quoted\" sea.jpg"_s;
        config.background.mode = shell::SessionConfig::Background::Mode::Tile;
        config.background.color = QColor(0x12, 0x34, 0x56);
        config.switcher.style = shell::SessionConfig::Switcher::Style::Icons;
        config.clock.seconds = true;
        config.power = {.blank = 0, .suspend = 45, .suspendOnBattery = 10};
        config.animations = {.overview = 0, .flip = 600, .flipStep = 90, .windows = 120};
        config.input.mouse = {.speed = -40, .scrollSpeed = 50, .naturalScroll = true};
        config.input.touchpad = {.speed = 25, .scrollSpeed = 150, .tapToClick = false, .disableWhileTyping = false};
        config.displays.primary = u"Dell U2720Q ABC123"_s;
        config.displays.keep({
            {.display = u"Dell U2720Q ABC123"_s, .size = QSize(3840, 2160), .refresh = 60000, .scale = 150},
            {.display = u"BOE 0x0BCA (eDP-1)"_s,
                .enabled = false,
                .size = QSize(1920, 1200),
                .refresh = 59950,
                .position = QPoint(2560, 0),
                .transform = 1},
        });
        QVERIFY(shell::saveSessionConfig(config, path));
        QCOMPARE(shell::loadSessionConfig(path), config);
    }

    void wrongValuesKeepDefaults()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"config.lua"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("return { background = { mode = \"sideways\", color = \"blue-ish\" }, switcher = 3 }\n");
        file.close();
        QCOMPARE(shell::loadSessionConfig(path), shell::SessionConfig());
    }

    void layoutsAreFoundByTheirDisplays()
    {
        shell::SessionConfig::Displays displays;
        displays.keep({{.display = u"A"_s}, {.display = u"B"_s, .position = QPoint(1920, 0)}});
        displays.keep({{.display = u"A"_s}});
        QVERIFY(displays.find({u"B"_s, u"A"_s}));
        QCOMPARE(displays.find({u"B"_s, u"A"_s})->at(1).position, QPoint(1920, 0));
        QVERIFY(displays.find({u"A"_s}));
        QVERIFY(!displays.find({u"B"_s}));

        // Kept again, the layout of the same displays takes the old one's place.
        displays.keep({{.display = u"A"_s, .scale = 200}});
        QCOMPARE(displays.layouts.size(), size_t(2));
        QCOMPARE(displays.find({u"A"_s})->front().scale, 200);
    }

    void colourAloneFillsTheBackground()
    {
        const shell::SessionConfig::Background background;
        const QImage image = shell::drawBackground(background, {}, QSize(40, 20));
        QCOMPARE(image.size(), QSize(40, 20));
        QCOMPARE(image.pixelColor(0, 0), background.color);
        QCOMPARE(image.pixelColor(39, 19), background.color);
    }

    void fittedPictureLeavesTheColourAround()
    {
        QImage picture(10, 10, QImage::Format_RGB32);
        picture.fill(Qt::red);
        shell::SessionConfig::Background background;
        background.mode = shell::SessionConfig::Background::Mode::Fit;
        const QImage image = shell::drawBackground(background, picture, QSize(40, 20));
        QCOMPARE(image.pixelColor(20, 10), QColor(Qt::red));
        QCOMPARE(image.pixelColor(2, 10), background.color);

        background.mode = shell::SessionConfig::Background::Mode::Fill;
        QCOMPARE(shell::drawBackground(background, picture, QSize(40, 20)).pixelColor(2, 10), QColor(Qt::red));
    }
};

QTEST_GUILESS_MAIN(TestSessionConfig)
#include "tst_sessionconfig.moc"
