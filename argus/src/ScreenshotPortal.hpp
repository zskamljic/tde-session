#pragma once

#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

namespace argus {

class Screenshot;

// The Screenshot of xdg-desktop-portal for TDE: programs that ask for a screenshot, or for a
// colour on the screen, have the user pick it the way Print does. Its slots are the portal's
// D-Bus methods; each is answered once the user is done.
class ScreenshotPortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Screenshot")
    Q_PROPERTY(uint version READ version CONSTANT)

public:
    static constexpr const char* ServiceName = "org.freedesktop.impl.portal.desktop.tde.argus";
    static constexpr const char* ObjectPath = "/org/freedesktop/portal/desktop";

    explicit ScreenshotPortal(argus::Screenshot& screenshot, QObject* parent = nullptr);

    bool registerOnBus();
    uint version() const { return 2; }

public slots:
    uint Screenshot(const QDBusObjectPath& handle, const QString& appId, const QString& parentWindow,
        const QVariantMap& options, QVariantMap& results);
    uint PickColor(const QDBusObjectPath& handle, const QString& appId, const QString& parentWindow,
        const QVariantMap& options, QVariantMap& results);

private:
    // Lets the portal close the request at `handle` early, until the answer is sent.
    void answerLater(const QDBusObjectPath& handle);
    void answer(uint response, const QVariantMap& results);

    argus::Screenshot& m_screenshot;
    QDBusMessage m_call; // the request waiting for its answer
    QString m_handle;
};

} // namespace argus
