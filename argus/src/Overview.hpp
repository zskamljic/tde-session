#pragma once

#include "Toplevels.hpp"

#include <Catalog.hpp>

#include <QRect>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <vector>

class QGraphicsOpacityEffect;
class QLineEdit;
class QToolButton;

namespace argus {

using shell::Application;

class AppGrid;

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

// Every open window at a glance, over everything else on the screen. Click a window to switch
// to it; Escape or a click on the background goes back. Typing searches the installed
// applications instead, and the button at the bottom shows all of them. Windows move from
// where they are into place as it opens, and back as it closes.
class Overview : public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Argus")

public:
    explicit Overview(Toplevels& toplevels, QWidget* parent = nullptr);

public slots:
    Q_SCRIPTABLE void Toggle();
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();
    // Shows all applications, or hides the overview when they are shown.
    Q_SCRIPTABLE void ToggleApplications();

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
    void closeOnto(quint64 chosen);
    void animateTo(double shown);
    // Where a window is on the screen, in this widget's coordinates; empty when not known.
    QRect frameOf(const Toplevel& window) const;
    bool settled() const { return m_shown >= 1 && !m_closing; }

    Toplevels& m_toplevels;
    QLineEdit* m_search = nullptr;
    QToolButton* m_appsButton = nullptr;
    AppGrid* m_grid = nullptr;
    shell::Catalog m_catalog;
    std::vector<Slot> m_slots;
    quint64 m_hovered = 0;
    bool m_hoveringClose = false;

    // How far it is shown, from 0 with the windows where they are to 1 with them in place.
    double m_shown = 0;
    QVariantAnimation m_animation;
    bool m_opening = false; // waiting for the pictures and places of the windows
    bool m_closing = false;
    quint64 m_chosen = 0; // the window it closes onto, drawn above the others
    QTimer m_patience; // how long the pictures are waited for
    std::vector<QGraphicsOpacityEffect*> m_fades; // of the search, the button and the grid
};

} // namespace argus
