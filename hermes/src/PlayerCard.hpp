#pragma once

#include <QFrame>

class QLabel;
class QToolButton;

namespace hermes {

class Media;

// The media player in charge, in the calendar's panel: its cover, what plays, and buttons to
// play, pause and skip. Hidden while nothing plays.
class PlayerCard : public QFrame {
public:
    explicit PlayerCard(Media& media, QWidget* parent = nullptr);

protected:
    // What plays is cut short to the width it has.
    void resizeEvent(QResizeEvent* event) override;

private:
    void sync();

    Media& m_media;
    QLabel* m_art;
    QLabel* m_title;
    QLabel* m_artist;
    QToolButton* m_previous;
    QToolButton* m_play;
    QToolButton* m_next;
};

} // namespace hermes
