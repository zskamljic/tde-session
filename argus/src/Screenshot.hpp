#pragma once

#include "Clipboard.hpp"
#include "ImageCopy.hpp"
#include "Toplevels.hpp"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QScreen>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace argus {

class ShotLayer;

// Screenshots, as GNOME takes them. Print freezes the screens and lets the user take part of
// one, a whole screen or a window; Shift+Print takes the screens and Alt+Print the window in use
// at once. Each is saved in Pictures/Screenshots, copied to the clipboard, and told of in a
// notification that shows it in the file manager. Its scriptable slots are its D-Bus methods.
// Other programs ask for screenshots and colours on the screen through the portal; theirs go to
// them instead.
class Screenshot : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Argus.Screenshot")

public:
    enum class Mode { Selection, Screen, Window };

    explicit Screenshot(Toplevels& toplevels, QObject* parent = nullptr);
    ~Screenshot() override;

    // What a screenshot asked for by another program gets: the picture, or a null one when the
    // user gave up.
    using Taken = std::function<void(QImage)>;
    using ColorPicked = std::function<void(std::optional<QColor>)>;

    // Where the buttons show.
    void setPrimaryScreen(QScreen* screen) { m_primary = screen; }

    // Busy with a screenshot, which another has to wait for.
    bool isBusy() const { return m_busy; }
    // Lets the user pick what to take, as Print does, for `taken`.
    void showFor(Taken taken);
    // Takes the screens at once for `taken`.
    void takeScreensFor(Taken taken);
    // Freezes the screens for the user to click a colour on, for `picked`.
    void pickColor(ColorPicked picked);
    // Saves a screenshot in Pictures/Screenshots; the path, or an empty one when it failed.
    static QString save(const QByteArray& png);

    // For the layers on the screens -----------------------------------------------------

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    const Toplevels& toplevels() const { return m_toplevels; }
    bool isPickingColor() const { return bool(m_colorPicked); }
    void pickColorAt(QScreen* screen, QPoint point);
    // What `screen` showed as it froze.
    QImage frozen(QScreen* screen) const;

    // The part of a screen to take, in its own coordinates.
    QScreen* selectionScreen() const { return m_selectionScreen; }
    QRect selection() const { return m_selection; }
    void select(QScreen* screen, const QRect& area);

    QScreen* chosenScreen() const { return m_chosenScreen; }
    void chooseScreen(QScreen* screen);

    // The window to take, by the id of the window list; 0 for none.
    quint64 chosenWindow() const { return m_chosenWindow; }
    void chooseWindow(quint64 id);
    // The topmost window shown at `point` of `screen`'s layer, if any.
    const Toplevel* windowAt(const QWidget& layer, QPoint point) const;

    // Takes what is picked, or closes without taking anything.
    void take();
    void cancel();

public slots:
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void TakeScreen();
    Q_SCRIPTABLE void TakeWindow();

private slots:
    void notificationAction(uint id, const QString& action);
    void notificationClosed(uint id, uint reason);

private:
    // Copies what every screen shows and asks where the windows are, then calls `then`.
    void freeze(std::function<void()> then);
    void frozenPart();
    void close();
    // The screens side by side as they are arranged, at the finest of their scales.
    QImage allScreens() const;
    // Saves `image`, copies it to the clipboard and says so.
    void keep(const QImage& image);
    void notify(const QString& path);
    void updateLayers();

    Toplevels& m_toplevels;
    ScreenCapture m_capture;
    Clipboard m_clipboard;
    QPointer<QScreen> m_primary;
    bool m_busy = false; // freezing, or shown

    std::map<QScreen*, QImage> m_frozen;
    std::map<QScreen*, std::unique_ptr<ImageCopy>> m_copies;
    bool m_locatingWindows = false;
    std::function<void()> m_then; // once frozen

    std::vector<std::unique_ptr<ShotLayer>> m_layers;
    Mode m_mode = Mode::Selection;
    // Kept between screenshots, as the same part is often taken again.
    QPointer<QScreen> m_selectionScreen;
    QRect m_selection;
    QPointer<QScreen> m_chosenScreen;
    quint64 m_chosenWindow = 0;

    Taken m_taken; // for another program, when it asked
    ColorPicked m_colorPicked;

    std::map<uint, QString> m_notified; // the files told of, by notification
};

} // namespace argus
