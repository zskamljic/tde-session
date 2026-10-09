#pragma once

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <vector>

class QDBusArgument;

namespace hermes {

struct Notification {
    enum class Urgency { Low, Normal, Critical };

    uint id = 0;
    QString appName;
    QString icon; // an icon name, or a file
    QImage image; // sent along with it, when there is one
    QString summary;
    QString body;
    QStringList actions; // key and label, one after the other
    Urgency urgency = Urgency::Normal;
    int timeout = -1; // in milliseconds; 0 for never, -1 for the server to decide
    bool transient = false; // gone once it was seen, not kept in the list
    bool resident = false; // stays after one of its actions was picked
    QString desktopEntry;
    QString soundFile; // to play instead of the usual sound
    QString soundName; // of the sound theme, likewise
    bool suppressSound = false;
    QDateTime time;
    bool banner = true; // still shown below the bar, not yet only in the list

    bool hasDefaultAction() const;
    // How long the banner stays, in milliseconds; 0 for as long as it takes.
    int bannerTime() const;
    // What to play when it arrives: a file of its own (a path), else the name of a sound of
    // the theme; empty for none.
    QString sound() const;
};

// A notification's body as markup that is safe to show: of the tags the spec allows, bold,
// italics, underlines, line breaks and links to web and mail addresses stay, everything
// else is reduced to its text. Line breaks of plain text are kept, and with `maxLength`,
// the text is cut there, never in the middle of a tag.
QString bodyMarkup(const QString& body, qsizetype maxLength = -1);

// The image-data hint: (iiibiiay) width, height, row stride, alpha, bits per sample,
// channels and the pixels.
QImage imageFromHint(const QDBusArgument& argument);

// The notification service of the desktop, org.freedesktop.Notifications: programs send
// notifications here, which stay until they time out (when they move to the list behind the
// clock), the user dismisses them, or the program closes them.
class NotificationServer : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    enum class Reason : uint { Expired = 1, Dismissed = 2, Closed = 3, Undefined = 4 };

    explicit NotificationServer(QObject* parent = nullptr);

    // Takes the service's name on the session bus; false when another program has it.
    bool start();

    // Newest first.
    const std::vector<Notification>& notifications() const { return m_notifications; }
    const Notification* find(uint id) const;

    // The user picked one of its actions, "default" for a click on the notification itself.
    // With a token, the program may bring its window forward.
    void invoke(uint id, const QString& action, const QString& activationToken = {});
    void dismiss(uint id);
    // Its banner's time is up: it stays in the list, unless it was transient.
    void expire(uint id);
    void dismissAll();
    // Do not disturb: notifications go to the list without a banner or a sound, unless they
    // are critical.
    bool isQuiet() const { return m_quiet; }
    void setQuiet(bool quiet);

public slots:
    Q_SCRIPTABLE QStringList GetCapabilities() const;
    Q_SCRIPTABLE uint Notify(const QString& appName, uint replacesId, const QString& appIcon, const QString& summary,
        const QString& body, const QStringList& actions, const QVariantMap& hints, int expireTimeout);
    Q_SCRIPTABLE void CloseNotification(uint id);
    Q_SCRIPTABLE QString GetServerInformation(QString& vendor, QString& version, QString& specVersion) const;

signals:
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);
    Q_SCRIPTABLE void ActionInvoked(uint id, const QString& actionKey);
    Q_SCRIPTABLE void ActivationToken(uint id, const QString& token);

    // For the bar: anything about the notifications changed.
    void changed();
    // A notification that was not there before; comes after `changed`.
    void arrived(uint id);
    // A notification took the place of another with the same id, and shows anew; comes
    // before `changed`.
    void replaced(uint id);

private:
    void close(uint id, Reason reason);

    std::vector<Notification> m_notifications;
    uint m_nextId = 1;
    bool m_quiet = false;
};

} // namespace hermes
