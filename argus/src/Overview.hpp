#pragma once

#include "Toplevels.hpp"
#include <Tween.hpp>

#include <Catalog.hpp>

#include <QRect>
#include <QPoint>
#include <QWidget>

#include <optional>
#include <vector>

class QGraphicsOpacityEffect;
class QLineEdit;
class QToolButton;

namespace argus {

using shell::Application;

class AppGrid;
class Canvas;
class Wallpaper;

// Where a window goes in the overview: its preview, scaled to fit, and its title below.
struct Slot {
    quint64 id = 0;
    QRect preview;
    QRect title;
    QRect closeButton;
};

// Lays windows of the given sizes out in rows, as large as the area allows but never larger
// than they are, the rows centred. Exposed for testing.
std::vector<QRect> layOut(const std::vector<QSize>& sizes, const QRect& area, int spacing, int titleHeight);

// The open windows of one screen at a glance, over everything else on it. Click a window to
// switch to it; Escape or a click on the background goes back. On the primary screen, typing
// searches the installed applications instead, and the button at the bottom shows all of
// them. Windows move from where they are into place as it opens, and back as it closes.
class Overview : public QWidget {
    Q_OBJECT

public:
    Overview(
        Toplevels& toplevels, const Wallpaper& wallpaper, QScreen* screen, bool primary, QWidget* parent = nullptr);
    ~Overview() override;

    // How long it takes to open or close, in milliseconds.
    void setAnimationTime(int milliseconds) { m_animationTime = milliseconds; }

    bool isOpen() const { return (isVisible() && !m_closing) || m_opening; }
    bool showsAllApplications() const;

    // Whether searching and the applications are here, with the keyboard.
    void setPrimary(bool primary);

    void Show();
    void showApplications();
    // Closes, with the window `chosen` coming forward, above the others; 0 for none.
    void closeOnto(quint64 chosen);
    // A window dragged from the overview of another screen is over this one at `pos`, or
    // left it for none.
    void showDrop(quint64 id, std::optional<QPoint> pos);
    // The windows moved: they are laid out again where they are now.
    void windowsMoved() { relayout(); }

signals:
    // Something here asks to close, with `chosen` coming forward: the overviews of all
    // screens close together.
    void closeRequested(quint64 chosen);
    // A window is dragged, to put it on another screen, and was dropped, at `global` in the
    // coordinates of the screens.
    void windowDragged(quint64 id, QPoint global);
    void windowDropped(quint64 id, QPoint global);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The colours of the theme, where they are not taken at every paint.
    void updateLook();
    // The windows over the desktop, on the canvas, which redraw() has drawn again.
    void paint(QPainter& painter);
    void redraw();
    bool showingApps() const;
    void updateMode();
    bool handleKey(QKeyEvent* event);
    void launch(const Application* app);
    void relayout();
    const Slot* slotAt(QPoint pos) const;
    void setHovered(quint64 id);
    void activate(quint64 id);
    void appear();
    void setLive(bool live);
    void animateTo(double shown);
    // Whether `window` is shown here: those on this screen, and on the primary screen those
    // not on any.
    bool showsWindow(const Toplevel& window) const;
    bool settled() const { return m_shown.now() >= 1 && !m_closing; }

    Toplevels& m_toplevels;
    const Wallpaper& m_wallpaper;
    bool m_primary;
    Canvas* m_canvas = nullptr;
    QLineEdit* m_search = nullptr;
    QToolButton* m_appsButton = nullptr;
    AppGrid* m_grid = nullptr;
    std::vector<Slot> m_slots;
    quint64 m_hovered = 0;
    bool m_hoveringClose = false;

    // How far it is shown, from 0 with the windows where they are to 1 with them in place.
    shell::Tween m_shown;
    int m_animationTime = 250;
    bool m_opening = false; // waiting for the pictures and places of the windows
    bool m_closing = false;
    quint64 m_chosen = 0; // the window it closes onto, drawn above the others
    bool m_live = false; // the pictures follow the windows
    // A window held by the pointer, and dragged once it moved far enough.
    quint64 m_pressed = 0;
    QPoint m_pressPos;
    bool m_dragging = false;
    QPoint m_dragPos;
    // A window dragged here from another screen.
    quint64 m_dropId = 0;
    QPoint m_dropPos;
    std::vector<QGraphicsOpacityEffect*> m_fades; // of the search, the button and the grid
};

} // namespace argus
