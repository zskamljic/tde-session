#pragma once

#include "Toplevels.hpp"

#include <QElapsedTimer>
#include <QPointer>
#include <QScreen>
#include <QWidget>

#include <vector>

namespace argus {

// Picks a window while a modifier is held, as Alt+Tab and Super+Tab do: it opens with the
// window used before the current one picked, every Tab picks the next, and letting go of the
// modifier, which the compositor tells, switches to the one picked. Escape goes back to the
// window in use. The windows are in the order they were used, most recent first.
class Picker : public QWidget {
    Q_OBJECT

public:
    Picker(Toplevels& toplevels, const QString& scope, QWidget* parent = nullptr);

    // Opens with the next window picked, or picks the next one when open. With
    // `sameApplication`, only the windows of the application in use take part.
    void pick(bool backwards, bool sameApplication);
    // The modifier was let go: the window picked is switched to. The compositor tells, as it
    // may happen before this has the keyboard, or without a key of its own.
    void release();
    // Where it shows from the next time it opens.
    void setScreen(QScreen* screen) { m_screen = screen; }

protected:
    // What subclasses show follows along: opened once shown, moved after the pick changed,
    // and closing once a window was chosen, after which finish() hides it.
    virtual void opened() { }
    virtual void moved() { update(); }
    virtual void closing() { finish(); }
    void finish();

    // Switches to the window at `index`, or back to the one in use for -1.
    void choose(int index);
    void move(int by);

    int count() const { return int(m_order.size()); }
    const Toplevel* window(int index) const;
    // How far the pick moved since opening, backwards below 0.
    int moves() const { return m_moves; }
    int picked() const;
    bool isClosing() const { return m_closing; }
    quint64 chosen() const { return m_chosen; }

    void keyPressEvent(QKeyEvent* event) override;

    Toplevels& m_toplevels;

private:
    void appear();
    void setKeyboard(bool taken);

    std::vector<quint64> m_order;
    int m_moves = 0;
    bool m_opening = false; // waiting for the pictures and places of the windows
    bool m_closing = false;
    bool m_sameApplication = false;
    bool m_released = false; // let go of while opening
    QElapsedTimer m_releasedEarly; // let go of before it was asked for, as the bus may have it
    int m_pendingMoves = 0; // asked for while opening
    quint64 m_chosen = 0;
    QPointer<QScreen> m_screen;
};

} // namespace argus
