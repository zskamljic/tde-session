#pragma once

#include "ImageCopy.hpp"
#include "Toplevels.hpp"

#include <QDBusContext>
#include <QDBusMessage>
#include <QPointer>
#include <QScreen>
#include <QWidget>

#include <map>
#include <memory>

class QAbstractButton;
class QButtonGroup;
class QGridLayout;
class QHBoxLayout;
class QPushButton;

namespace argus {

// Asks what to share when a program wants to record the screen: a whole screen or one window,
// shown as they look now. xdg-desktop-portal-wlr runs `tde-argus --choose-shared` to ask, which
// asks this over the session bus and prints the answer, as the portal reads it.
class ShareChooser : public QWidget, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Argus.Sharing")

public:
    explicit ShareChooser(Toplevels& toplevels, QWidget* parent = nullptr);
    ~ShareChooser() override;

    // Where it shows.
    void setPrimaryScreen(QScreen* screen) { m_primary = screen; }

public slots:
    // "Monitor: <output>" or "Window: <identifier>", as xdg-desktop-portal-wlr wants it; empty when
    // the user did not want to share. Answered once the user picked.
    Q_SCRIPTABLE QString Choose();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void appear();
    void answer(const QString& choice);
    void placePanel();
    QAbstractButton* addTile(QWidget* parent, const QImage& picture, const QString& name, const QString& choice);

    Toplevels& m_toplevels;
    ScreenCapture m_capture;
    QPointer<QScreen> m_primary;
    std::map<QScreen*, std::unique_ptr<ImageCopy>> m_copies;
    std::map<QScreen*, QImage> m_pictures;
    bool m_waitingForWindows = false;
    QDBusMessage m_call; // the question waiting for its answer
    bool m_asking = false;

    QWidget* m_panel = nullptr;
    QHBoxLayout* m_screenRow = nullptr;
    QGridLayout* m_windowGrid = nullptr;
    QButtonGroup* m_tiles = nullptr;
    QPushButton* m_share = nullptr;
};

} // namespace argus
