#pragma once

#include <SessionConfig.hpp>

#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QWidget>

#include <map>

class QPainter;

namespace argus {

// The desktop's background as the session's settings have it, drawn to the size of a screen
// once and kept, for the background and for what shows behind the windows while switching.
class Wallpaper : public QObject {
    Q_OBJECT

public:
    using Settings = shell::SessionConfig::Background;

    void setSettings(const Settings& settings);
    // The whole picture for an area of `size`, at `ratio` device pixels to each.
    const QPixmap& pixmap(QSize size, qreal ratio) const;

signals:
    void changed();

private:
    Settings m_settings;
    QImage m_picture; // as it is in the file
    mutable std::map<std::pair<int, int>, QPixmap> m_pixmaps; // by size, for screens of each
    bool m_loaded = false;
};

// What shows behind the windows in the overview and Flip 3D: the desktop, dimmed as far as
// `shown` goes from 0 to 1.
void paintBackdrop(QPainter& painter, const QWidget& widget, const Wallpaper& wallpaper, double shown);

// The desktop's background, below every window.
class Background : public QWidget {
public:
    Background(const Wallpaper& wallpaper, QScreen* screen, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const Wallpaper& m_wallpaper;
};

} // namespace argus
