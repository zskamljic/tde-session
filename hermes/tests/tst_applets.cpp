#include "LuaApplet.hpp"
#include "SystemUsage.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using hermes::LuaApplet;
using hermes::SystemUsage;

class TestApplets : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    SystemUsage m_usage;

    // An applet made of `code`, in a file of its own.
    std::unique_ptr<LuaApplet> applet(const QByteArray& code)
    {
        static int count = 0;
        const QString path = m_dir.filePath(u"applet%1.lua"_s.arg(++count));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return nullptr;
        file.write(code);
        file.close();
        return std::make_unique<LuaApplet>(path, m_usage);
    }

    static QImage painted(LuaApplet& applet, QSize size, bool popup = false)
    {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        if (popup)
            applet.paintPopup(painter, size);
        else
            applet.paint(painter, size);
        return image;
    }

private slots:
    void parsesStat()
    {
        const auto times = SystemUsage::parseStat("cpu  10 0 10 70 10 0 0 0 0 0\n"
                                                  "cpu0 5 0 5 35 5 0 0 0 0 0\n"
                                                  "cpu1 5 0 5 35 5 0 0 0 0 0\n"
                                                  "intr 1 2 3\n");
        QCOMPARE(times.size(), size_t(3));
        QCOMPARE(times[0].total, 100u);
        QCOMPARE(times[0].idle, 80u);
    }

    void tellsUsageBetweenReadings()
    {
        const std::vector<SystemUsage::CpuTimes> before {{100, 80}, {50, 40}, {50, 40}};
        const std::vector<SystemUsage::CpuTimes> after {{200, 130}, {100, 90}, {100, 40}};
        const auto usage = SystemUsage::usage(before, after);
        QCOMPARE(usage.total, 50.0);
        QCOMPARE(usage.cores, (std::vector<double> {0.0, 100.0}));
    }

    void parsesMeminfo()
    {
        const auto memory = SystemUsage::parseMeminfo("MemTotal:       1000 kB\n"
                                                      "MemFree:         100 kB\n"
                                                      "MemAvailable:    400 kB\n"
                                                      "SwapTotal:       500 kB\n"
                                                      "SwapFree:        500 kB\n");
        QCOMPARE(memory.total, 1024000u);
        QCOMPARE(memory.used, 614400u);
        QCOMPARE(memory.swapTotal, 512000u);
        QCOMPARE(memory.swapUsed, 0u);
    }

    void parsesNetDevWithoutLoopback()
    {
        const auto totals = SystemUsage::parseNetDev(
            "Inter-|   Receive                            |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets\n"
            "    lo: 9000 1 0 0 0 0 0 0 9000 1 0 0 0 0 0 0\n"
            "  eth0: 1000 1 0 0 0 0 0 0 200 1 0 0 0 0 0 0\n"
            " wlan0:  500 1 0 0 0 0 0 0  50 1 0 0 0 0 0 0\n");
        QCOMPARE(totals.received, 1500u);
        QCOMPARE(totals.sent, 250u);
    }

    void readsSettings()
    {
        auto a = applet("return { width = 80, interval = 500, popup = { width = 300, height = 200 } }");
        QVERIFY(a->load());
        QCOMPARE(a->width(), 80);
        QCOMPARE(a->interval(), 500);
        QCOMPARE(a->popupSize(), QSize(300, 200));
    }

    void paintsInTheBarAndThePopup()
    {
        auto a
            = applet("local applet = { popup = { width = 20, height = 20 } }\n"
                     "function applet:paint(canvas) canvas:rect(0, 0, canvas.width, canvas.height, \"#ff0000\") end\n"
                     "applet.popup.paint = function(self, canvas) canvas:rect(0, 0, 10, 10, \"#0000ff\") end\n"
                     "return applet\n");
        QVERIFY(a->load());
        QCOMPARE(painted(*a, QSize(10, 10)).pixelColor(5, 5), QColor(Qt::red));
        const QImage popup = painted(*a, QSize(20, 20), true);
        QCOMPARE(popup.pixelColor(5, 5), QColor(Qt::blue));
        QCOMPARE(popup.pixelColor(15, 15).alpha(), 0);
    }

    void givesDataAndTheme()
    {
        auto a = applet("local applet = {}\n"
                        "function applet:update()\n"
                        "local cpu, memory, network = tde.cpu(), tde.memory(), tde.network()\n"
                        "assert(type(cpu.total) == \"number\" and type(cpu.cores) == \"table\")\n"
                        "assert(memory.total > 0 and memory.used <= memory.total)\n"
                        "assert(network.down >= 0 and network.up >= 0)\n"
                        "assert(tde.theme.text:match(\"^#%x%x%x%x%x%x%x%x$\"))\n"
                        "assert(tde.api == 1)\n"
                        "self.ok = true\n"
                        "end\n"
                        "function applet:paint(canvas)\n"
                        "if self.ok then canvas:rect(0, 0, 4, 4, \"#00ff00\") end\n"
                        "end\n"
                        "return applet\n");
        QVERIFY(a->load());
        a->update();
        QCOMPARE(a->error(), QString());
        QCOMPARE(painted(*a, QSize(4, 4)).pixelColor(1, 1), QColor(Qt::green));
    }

    void leavesOutFilesAndPrograms()
    {
        auto a = applet("assert(io == nil and os == nil and dofile == nil and loadfile == nil and require == nil)\n"
                        "return {}");
        QVERIFY(a->load());
    }

    void survivesErrors()
    {
        QVERIFY(!applet("return 42")->load());
        QVERIFY(!applet("this is not Lua")->load());
        auto a = applet("return {\n"
                        "update = function() error(\"broken\") end,\n"
                        "paint = function(self, canvas) canvas:rect(0, 0, 1, 1, \"not a colour\") end,\n"
                        "}\n");
        QVERIFY(a->load());
        a->update();
        QVERIFY(a->error().contains(u"broken"_s));
        painted(*a, QSize(4, 4));
        QVERIFY(a->error().contains(u"colour"_s));
    }

    void stopsWhatTakesTooLong()
    {
        auto a = applet("return { update = function() while true do end end }");
        QVERIFY(a->load());
        QElapsedTimer timer;
        timer.start();
        a->update();
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(a->error().contains(u"stopped"_s));
    }

    void keepsTheCanvasToPaint()
    {
        auto a = applet("local applet = {}\n"
                        "function applet:paint(canvas) self.kept = canvas end\n"
                        "function applet:update() self.kept:rect(0, 0, 1, 1, \"#ffffff\") end\n"
                        "return applet\n");
        QVERIFY(a->load());
        painted(*a, QSize(4, 4));
        a->update();
        QVERIFY(a->error().contains(u"only within paint"_s));
    }
};

QTEST_MAIN(TestApplets)
#include "tst_applets.moc"
