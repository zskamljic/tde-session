#include "Media.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QNetworkReply>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

const QString Prefix = u"org.mpris.MediaPlayer2."_s;
const QString Path = u"/org/mpris/MediaPlayer2"_s;
const QString Root = u"org.mpris.MediaPlayer2"_s;
const QString PlayerInterface = u"org.mpris.MediaPlayer2.Player"_s;
const QString Properties = u"org.freedesktop.DBus.Properties"_s;

QVariantMap allProperties(const QString& service, const QString& interface)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, Path, Properties, u"GetAll"_s);
    call << interface;
    const QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 1000);
    return reply.isValid() ? reply.value() : QVariantMap();
}

// Metadata comes as a{sv} wrapped in a D-Bus argument.
QVariantMap metadataOf(const QVariant& value)
{
    if (value.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}

} // namespace

Media::Media(QObject* parent)
    : QObject(parent)
{
    auto bus = QDBusConnection::sessionBus();
    bus.connect(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s, u"org.freedesktop.DBus"_s,
        u"NameOwnerChanged"_s, this, SLOT(nameOwnerChanged(QString, QString, QString)));
    for (const QString& name : bus.interface()->registeredServiceNames().value()) {
        if (name.startsWith(Prefix))
            add(name);
    }
}

const Player* Media::current() const
{
    return m_players.empty() ? nullptr : &m_players.front();
}

void Media::nameOwnerChanged(const QString& name, const QString&, const QString& newOwner)
{
    if (!name.startsWith(Prefix))
        return;
    std::erase_if(m_players, [&](const Player& player) { return player.service == name; });
    if (!newOwner.isEmpty())
        add(name);
    else
        emit changed();
    fetchArt();
}

void Media::add(const QString& service)
{
    Player player;
    player.service = service;
    const QVariantMap root = allProperties(service, Root);
    player.identity = root.value(u"Identity"_s).toString();
    player.desktopEntry = root.value(u"DesktopEntry"_s).toString();
    refresh(player);
    // Those playing go first; the rest after the ones there are.
    if (player.playing)
        m_players.insert(m_players.begin(), std::move(player));
    else
        m_players.push_back(std::move(player));
    QDBusConnection::sessionBus().connect(
        service, Path, Properties, u"PropertiesChanged"_s, this, SLOT(propertiesChanged()));
    emit changed();
    fetchArt();
}

void Media::refresh(Player& player)
{
    const QVariantMap properties = allProperties(player.service, PlayerInterface);
    const QVariantMap metadata = metadataOf(properties.value(u"Metadata"_s));
    player.title = metadata.value(u"xesam:title"_s).toString();
    player.artist = metadata.value(u"xesam:artist"_s).toStringList().join(u", "_s);
    player.artUrl = metadata.value(u"mpris:artUrl"_s).toString();
    player.playing = properties.value(u"PlaybackStatus"_s).toString() == u"Playing";
    player.canGoNext = properties.value(u"CanGoNext"_s).toBool();
    player.canGoPrevious = properties.value(u"CanGoPrevious"_s).toBool();
    player.canControl = properties.value(u"CanControl"_s, true).toBool();
}

void Media::propertiesChanged()
{
    // The signal does not say which player, as several share the path: the sender does.
    const QString sender = message().service();
    bool found = false;
    for (auto it = m_players.begin(); it != m_players.end(); ++it) {
        const QString owner = QDBusConnection::sessionBus().interface()->serviceOwner(it->service).value();
        if (it->service != sender && owner != sender)
            continue;
        found = true;
        const bool wasPlaying = it->playing;
        refresh(*it);
        // One that starts playing takes charge.
        if (it->playing && !wasPlaying)
            std::rotate(m_players.begin(), it, it + 1);
        break;
    }
    if (found) {
        emit changed();
        fetchArt();
    }
}

void Media::call(const QString& method)
{
    const Player* player = current();
    if (!player)
        return;
    QDBusConnection::sessionBus().call(
        QDBusMessage::createMethodCall(player->service, Path, PlayerInterface, method), QDBus::NoBlock);
}

void Media::playPause()
{
    call(u"PlayPause"_s);
}

void Media::next()
{
    call(u"Next"_s);
}

void Media::previous()
{
    call(u"Previous"_s);
}

void Media::stop()
{
    call(u"Stop"_s);
}

void Media::fetchArt()
{
    const Player* player = current();
    const QString url = player ? player->artUrl : QString();
    if (url == m_artUrl)
        return;
    m_artUrl = url;
    m_art = {};
    const QUrl location(url);
    if (location.isLocalFile()) {
        m_art.load(location.toLocalFile());
    } else if (location.scheme() == u"https" || location.scheme() == u"http") {
        QNetworkReply* reply = m_network.get(QNetworkRequest(location));
        connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
            reply->deleteLater();
            if (url == m_artUrl && reply->error() == QNetworkReply::NoError && m_art.loadFromData(reply->readAll()))
                emit changed();
        });
        return;
    }
    emit changed();
}

} // namespace hermes
