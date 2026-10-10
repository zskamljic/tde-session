#pragma once

#include <QDBusContext>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace hermes {

// A program playing music or video, as MPRIS tells it.
struct Player {
    QString service; // its name on the session bus
    QString owner; // the unique one, which its signals come from
    QString identity; // "Firefox", "Spotify"
    QString desktopEntry;
    QString title;
    QString artist;
    QString artUrl;
    bool playing = false;
    bool canGoNext = false;
    bool canGoPrevious = false;
    bool canControl = false;
};

// The media players on the session bus, and the one in charge: the one that played last.
// Media keys and the player in the calendar's panel control that one.
class Media : public QObject, protected QDBusContext {
    Q_OBJECT

public:
    explicit Media(QObject* parent = nullptr);

    // The one in charge; null when no program plays anything.
    const Player* current() const;
    // Its cover, once fetched; null when it has none.
    const QImage& art() const { return m_art; }

    void playPause();
    void next();
    void previous();
    void stop();

signals:
    void changed();

private slots:
    void nameOwnerChanged(const QString& name, const QString& oldOwner, const QString& newOwner);
    void propertiesChanged();

private:
    void add(const QString& service, const QString& owner);
    void refresh(Player& player);
    void call(const QString& method);
    void fetchArt();

    std::vector<Player> m_players; // the one played last first
    QNetworkAccessManager m_network;
    QString m_artUrl; // of the cover in m_art
    QImage m_art;
};

} // namespace hermes
