#include "ScreenshotPortal.hpp"

#include "Screenshot.hpp"

#include <QBuffer>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace argus {
namespace {

// What the portal hears back: done, given up on, or ended some other way.
enum Response : uint { Done = 0, Cancelled = 1, Ended = 2 };

// A colour as the portal sends it: red, green and blue from 0 to 1.
struct PortalColor {
    double red = 0;
    double green = 0;
    double blue = 0;
};

QDBusArgument& operator<<(QDBusArgument& argument, const PortalColor& color)
{
    argument.beginStructure();
    argument << color.red << color.green << color.blue;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, PortalColor& color)
{
    argument.beginStructure();
    argument >> color.red >> color.green >> color.blue;
    argument.endStructure();
    return argument;
}

// The portal's handle on a request, through which it may close it early.
class Request : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
    explicit Request(Screenshot& screenshot, QObject* parent)
        : QObject(parent)
        , m_screenshot(screenshot)
    {
    }

public slots:
    void Close() { m_screenshot.cancel(); }

private:
    Screenshot& m_screenshot;
};

bool option(const QVariantMap& options, const QString& name)
{
    return options.value(name).toBool();
}

} // namespace
} // namespace argus

Q_DECLARE_METATYPE(argus::PortalColor)

namespace argus {

ScreenshotPortal::ScreenshotPortal(argus::Screenshot& screenshot, QObject* parent)
    : QObject(parent)
    , m_screenshot(screenshot)
{
    qDBusRegisterMetaType<PortalColor>();
}

bool ScreenshotPortal::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->registerService(QString::fromLatin1(ServiceName)).value())
        return false;
    return bus.registerObject(
        QString::fromLatin1(ObjectPath), this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
}

uint ScreenshotPortal::Screenshot(
    const QDBusObjectPath& handle, const QString&, const QString&, const QVariantMap& options, QVariantMap&)
{
    if (m_screenshot.isBusy())
        return Ended;
    answerLater(handle);
    const auto taken = [this](const QImage& image) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        const QString path = image.isNull() || !image.save(&buffer, "PNG") ? QString() : argus::Screenshot::save(png);
        if (path.isEmpty())
            answer(image.isNull() ? Cancelled : Ended, {});
        else
            answer(Done, {{u"uri"_s, QUrl::fromLocalFile(path).toString()}});
    };
    if (option(options, u"interactive"_s))
        m_screenshot.showFor(taken);
    else
        m_screenshot.takeScreensFor(taken);
    return Ended;
}

uint ScreenshotPortal::PickColor(
    const QDBusObjectPath& handle, const QString&, const QString&, const QVariantMap&, QVariantMap&)
{
    if (m_screenshot.isBusy())
        return Ended;
    answerLater(handle);
    m_screenshot.pickColor([this](std::optional<QColor> color) {
        if (!color) {
            answer(Cancelled, {});
            return;
        }
        const PortalColor picked {.red = color->redF(), .green = color->greenF(), .blue = color->blueF()};
        answer(Done, {{u"color"_s, QVariant::fromValue(picked)}});
    });
    return Ended;
}

void ScreenshotPortal::answerLater(const QDBusObjectPath& handle)
{
    setDelayedReply(true);
    m_call = message();
    m_handle = handle.path();
    connection().registerObject(m_handle, new Request(m_screenshot, this), QDBusConnection::ExportAllSlots);
}

void ScreenshotPortal::answer(uint response, const QVariantMap& results)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (QObject* request = bus.objectRegisteredAt(m_handle)) {
        bus.unregisterObject(m_handle);
        request->deleteLater();
    }
    bus.send(m_call.createReply({QVariant::fromValue(response), QVariant::fromValue(results)}));
}

} // namespace argus

#include "ScreenshotPortal.moc"
