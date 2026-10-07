#pragma once

#include "Toplevels.hpp"
#include <Tween.hpp>

#include <Catalog.hpp>

#include <QRect>
#include <QWidget>

#include <vector>

class QGraphicsOpacityEffect;
class QLineEdit;
class QToolButton;

namespace argus {

using shell::Application;

class AppGrid;
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

signals:
    // Something here asks to close, with `chosen` coming forward: the overviews of all
    // screens close together.
    void closeRequested(quint64 chosen);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool showingApps() const;
    void updateMode();
    bool handleKey(QKeyEvent* event);
    void launch(const Application* app);
    void relayout();
    const Slot* slotAt(QPoint pos) const;
    void setHovered(quint64 id);
    void activate(quint64 id);
    void appear();
    void animateTo(double shown);
    // Whether `window` is shown here: those on this screen, and on the primary screen those
    // not on any.
    bool showsWindow(const Toplevel& window) const;
    bool settled() const { return m_shown.now() >= 1 && !m_closing; }

    Toplevels& m_toplevels;
    const Wallpaper& m_wallpaper;
    bool m_primary;
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
    std::vector<QGraphicsOpacityEffect*> m_fades; // of the search, the button and the grid
};

} // namespace argus
