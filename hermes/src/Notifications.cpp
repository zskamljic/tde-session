#include "Notifications.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

constexpr int DefaultBannerTime = 5000;
constexpr size_t MaxKept = 50;

// An icon given as a file URL or path, or the name of one from the theme.
QString iconOf(const QString& value)
{
    if (value.startsWith(u"file://"))
        return QUrl(value).toLocalFile();
    return value;
}

} // namespace

bool Notification::hasDefaultAction() const
{
    for (qsizetype i = 0; i + 1 < actions.size(); i += 2) {
        if (actions[i] == u"default")
            return true;
    }
    return false;
}

int Notification::bannerTime() const
{
    if (timeout > 0)
        return timeout;
    if (timeout == 0 || urgency == Urgency::Critical)
        return 0;
    return DefaultBannerTime;
}

QString Notification::sound() const
{
    // Quiet ones stay quiet, as do those that ask for no sound.
    if (suppressSound || urgency == Urgency::Low)
        return {};
    if (!soundFile.isEmpty())
        return soundFile;
    if (!soundName.isEmpty())
        return soundName;
    return urgency == Urgency::Critical ? u"dialog-warning"_s : u"message-new-instant"_s;
}

QString bodyMarkup(const QString& whole, qsizetype maxLength)
{
    static const QRegularExpression tag(u"<(/?)([a-zA-Z]+)([^<>]*)>"_s);
    static const QRegularExpression entity(u"^&(#[0-9]+|#x[0-9a-fA-F]+|[a-zA-Z]+);"_s);
    static const QRegularExpression href(
        u"href\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"_s, QRegularExpression::CaseInsensitiveOption);
    static const QStringList kept {u"b"_s, u"i"_s, u"u"_s, u"a"_s};

    // Bounds for what a program could send to keep the bar busy: no notification needs more.
    constexpr qsizetype MaxBody = 64 * 1024;
    constexpr qsizetype MaxNesting = 16;
    const QString body = whole.left(MaxBody);

    QString result;
    QStringList open; // the tags open, innermost last
    qsizetype length = 0;
    bool cut = false;
    bool textFollows = true; // once there is none after a tag, there is none after later ones

    // Text between tags: kept as it is, entities and all, but for what would start markup.
    const auto addText = [&](QStringView text) {
        for (qsizetype i = 0; i < text.size() && !cut; ++i) {
            if (maxLength >= 0 && length >= maxLength) {
                result += u'…';
                cut = true;
                break;
            }
            const QChar c = text[i];
            if (c == u'&') {
                const auto match = entity.matchView(text.mid(i));
                if (match.hasMatch()) {
                    result += match.captured();
                    i += match.capturedLength() - 1;
                } else {
                    result += u"&amp;";
                }
            } else if (c == u'<') {
                result += u"&lt;";
            } else if (c == u'>') {
                result += u"&gt;";
            } else if (c == u'\n') {
                result += u"<br>";
            } else {
                result += c;
            }
            ++length;
        }
    };

    qsizetype at = 0;
    for (auto it = tag.globalMatchView(body); it.hasNext() && !cut;) {
        const auto match = it.next();
        addText(QStringView(body).mid(at, match.capturedStart() - at));
        at = match.capturedEnd();
        if (cut)
            break;
        // Full, with text still to come: cut before the tag rather than inside it.
        if (maxLength >= 0 && length >= maxLength && textFollows)
            textFollows = !QString(body.mid(match.capturedStart())).remove(tag).trimmed().isEmpty();
        if (maxLength >= 0 && length >= maxLength && textFollows) {
            result += u'…';
            cut = true;
            break;
        }
        const bool closing = !match.capturedView(1).isEmpty();
        const QString name = match.captured(2).toLower();
        if (name == u"br") {
            result += u"<br>";
        } else if (!kept.contains(name) || (!closing && open.size() >= MaxNesting)) {
            continue; // its text stays, the tag goes
        } else if (closing) {
            // Only what is open closes, and all that was opened inside it with it.
            const qsizetype index = open.lastIndexOf(name);
            while (index >= 0 && open.size() > index)
                result += u"</"_s + open.takeLast() + u'>';
        } else if (name == u"a") {
            const auto link = href.match(match.captured(3));
            const QString target = link.captured(1).isEmpty() ? link.captured(2) : link.captured(1);
            const QString scheme = QUrl(target).scheme().toLower();
            if (scheme == u"http" || scheme == u"https" || scheme == u"mailto") {
                result += u"<a href=\""_s + target.toHtmlEscaped() + u"\">"_s;
                open << name;
            }
        } else {
            result += u'<' + name + u'>';
            open << name;
        }
    }
    // What is left, unless the cut is reached and it is only space.
    const QStringView rest = QStringView(body).mid(at);
    if (!cut && !(maxLength >= 0 && length >= maxLength && rest.trimmed().isEmpty()))
        addText(rest);
    while (!open.isEmpty())
        result += u"</"_s + open.takeLast() + u'>';
    return result;
}

QImage imageFromHint(const QDBusArgument& argument)
{
    int width = 0;
    int height = 0;
    int stride = 0;
    bool alpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray data;
    argument.beginStructure();
    argument >> width >> height >> stride >> alpha >> bitsPerSample >> channels >> data;
    argument.endStructure();
    const qint64 row = qint64(width) * channels;
    if (width <= 0 || height <= 0 || bitsPerSample != 8 || (channels != 3 && channels != 4) || stride < row
        || data.size() < qint64(stride) * (height - 1) + row)
        return {};
    const QImage::Format format = channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    // A copy, as the image would otherwise point into the data that goes away.
    return QImage(reinterpret_cast<const uchar*>(data.constData()), width, height, stride, format).copy();
}

NotificationServer::NotificationServer(QObject* parent)
    : QObject(parent)
{
}

bool NotificationServer::start()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(u"org.freedesktop.Notifications"_s))
        return false;
    return bus.registerObject(u"/org/freedesktop/Notifications"_s, this,
        QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
}

const Notification* NotificationServer::find(uint id) const
{
    const auto it = std::ranges::find(m_notifications, id, &Notification::id);
    return it == m_notifications.end() ? nullptr : &*it;
}

QStringList NotificationServer::GetCapabilities() const
{
    return {u"actions"_s, u"body"_s, u"body-markup"_s, u"body-hyperlinks"_s, u"icon-static"_s, u"persistence"_s};
}

QString NotificationServer::GetServerInformation(QString& vendor, QString& version, QString& specVersion) const
{
    vendor = u"TDE"_s;
    version = QStringLiteral(TDE_SESSION_VERSION);
    specVersion = u"1.2"_s;
    return u"Hermes"_s;
}

uint NotificationServer::Notify(const QString& appName, uint replacesId, const QString& appIcon, const QString& summary,
    const QString& body, const QStringList& actions, const QVariantMap& hints, int expireTimeout)
{
    Notification notification;
    notification.appName = appName;
    notification.icon = iconOf(appIcon);
    notification.summary = summary;
    notification.body = body;
    notification.actions = actions;
    notification.timeout = expireTimeout;
    notification.time = QDateTime::currentDateTime();

    const auto hint = [&](const char* name) { return hints.value(QString::fromLatin1(name)); };
    if (const QVariant urgency = hint("urgency"); urgency.isValid())
        notification.urgency = static_cast<Notification::Urgency>(std::clamp(urgency.toInt(), 0, 2));
    notification.transient = hint("transient").toBool();
    notification.resident = hint("resident").toBool();
    notification.desktopEntry = hint("desktop-entry").toString();
    notification.soundFile = iconOf(hint("sound-file").toString());
    notification.soundName = hint("sound-name").toString();
    notification.suppressSound = hint("suppress-sound").toBool();
    // The image, by the names the versions of the spec gave it.
    for (const char* name : {"image-data", "image_data", "icon_data"}) {
        if (const QVariant value = hint(name); value.canConvert<QDBusArgument>()) {
            notification.image = imageFromHint(value.value<QDBusArgument>());
            break;
        }
    }
    for (const char* name : {"image-path", "image_path"}) {
        if (const QString path = hint(name).toString(); !path.isEmpty() && notification.image.isNull()) {
            if (const QString file = iconOf(path); file.startsWith(u'/'))
                notification.image.load(file);
            else if (notification.icon.isEmpty())
                notification.icon = file;
        }
    }

    // A replacement takes the place of the one it replaces, and shows again.
    const auto previous
        = replacesId ? std::ranges::find(m_notifications, replacesId, &Notification::id) : m_notifications.end();
    const bool replacing = previous != m_notifications.end();
    uint id = 0;
    if (replacing) {
        id = notification.id = replacesId;
        *previous = std::move(notification);
    } else {
        id = notification.id = m_nextId++;
        if (m_nextId == 0)
            m_nextId = 1;
        m_notifications.insert(m_notifications.begin(), std::move(notification));
        // The oldest ones go, once there are many.
        while (m_notifications.size() > MaxKept) {
            const uint oldest = m_notifications.back().id;
            m_notifications.pop_back();
            emit NotificationClosed(oldest, uint(Reason::Undefined));
        }
    }
    if (replacing)
        emit replaced(id);
    emit changed();
    if (!replacing)
        emit arrived(id);
    return id;
}

void NotificationServer::CloseNotification(uint id)
{
    close(id, Reason::Closed);
}

void NotificationServer::close(uint id, Reason reason)
{
    const auto it = std::ranges::find(m_notifications, id, &Notification::id);
    if (it == m_notifications.end())
        return;
    m_notifications.erase(it);
    emit NotificationClosed(id, uint(reason));
    emit changed();
}

void NotificationServer::invoke(uint id, const QString& action, const QString& activationToken)
{
    const Notification* notification = find(id);
    if (!notification)
        return;
    const bool resident = notification->resident;
    // The token first, so the program has it when it acts.
    if (!activationToken.isEmpty())
        emit ActivationToken(id, activationToken);
    emit ActionInvoked(id, action);
    if (!resident)
        close(id, Reason::Dismissed);
}

void NotificationServer::dismiss(uint id)
{
    close(id, Reason::Dismissed);
}

void NotificationServer::expire(uint id)
{
    const auto it = std::ranges::find(m_notifications, id, &Notification::id);
    if (it == m_notifications.end())
        return;
    if (it->transient) {
        close(id, Reason::Expired);
        return;
    }
    it->banner = false;
    emit changed();
}

void NotificationServer::dismissAll()
{
    std::vector<uint> ids;
    for (const Notification& notification : m_notifications) {
        if (!notification.banner)
            ids.push_back(notification.id);
    }
    for (const uint id : ids)
        close(id, Reason::Dismissed);
}

} // namespace hermes
