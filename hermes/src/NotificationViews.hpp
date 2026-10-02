#pragma once

#include <QFrame>
#include <QTimer>

#include <map>

class QVBoxLayout;
class QLabel;

namespace hermes {

class NotificationServer;
struct Notification;

// One notification: its icon, the application, summary, text and actions. A click on it
// picks its default action, or dismisses it; actions picked are handed on, for the bar to
// pass to the program with what it needs to bring its window forward.
class NotificationCard : public QFrame {
    Q_OBJECT

public:
    enum class Mode {
        Banner, // below the bar, until its time is up; the pointer over it stops the clock
        List, // in the list behind the clock
    };

    NotificationCard(
        NotificationServer& server, const Notification& notification, Mode mode, QWidget* parent = nullptr);

    uint id() const { return m_id; }
    // Takes it away: hidden and with its time stopped at once, deleted a moment later.
    void retire();

signals:
    // An action was picked or it was dismissed by the user.
    void handled();
    // An empty action for a click on one without a default action.
    void actionPicked(uint id, const QString& action);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    NotificationServer& m_server;
    uint m_id;
    bool m_hasDefault;
    QTimer m_expiry;
};

// The banners of new notifications, below the bar in the top right corner of the screen,
// the newest on top.
class Banners : public QWidget {
    Q_OBJECT

public:
    explicit Banners(NotificationServer& server, QWidget* parent = nullptr);

signals:
    void actionPicked(uint id, const QString& action);

private:
    void sync();

    NotificationServer& m_server;
    QVBoxLayout* m_layout;
    std::map<uint, NotificationCard*> m_cards;
};

// The notifications whose banners are gone, until they are dismissed: in the clock's menu.
class NotificationList : public QWidget {
    Q_OBJECT

public:
    explicit NotificationList(NotificationServer& server, QWidget* parent = nullptr);

signals:
    void handled();
    void actionPicked(uint id, const QString& action);

private:
    void rebuild();

    NotificationServer& m_server;
    QWidget* m_scroll;
    QVBoxLayout* m_cards;
    QLabel* m_empty;
    QWidget* m_clear;
};

} // namespace hermes
