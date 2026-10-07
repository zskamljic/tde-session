#include "Background.hpp"
#include <Layer.hpp>

#include <QPainter>

using namespace Qt::StringLiterals;

namespace argus {

void Wallpaper::setSettings(const Settings& settings)
{
    if (settings == m_settings && m_loaded)
        return;
    if (settings.image != m_settings.image || !m_loaded) {
        QString error;
        m_picture = settings.image.isEmpty() ? QImage() : shell::loadBackgroundPicture(settings.image, 1, &error);
        if (!settings.image.isEmpty() && m_picture.isNull())
            qWarning("tde-argus: cannot show %s: %s", qPrintable(settings.image), qPrintable(error));
    }
    m_settings = settings;
    m_loaded = true;
    m_pixmaps.clear();
    emit changed();
}

const QPixmap& Wallpaper::pixmap(QSize size, qreal ratio) const
{
    const QSize pixels = size * ratio;
    QPixmap& pixmap = m_pixmaps[{pixels.width(), pixels.height()}];
    if (pixmap.isNull() && !pixels.isEmpty())
        pixmap = QPixmap::fromImage(shell::drawBackground(m_settings, m_picture, pixels));
    return pixmap;
}

void paintBackdrop(QPainter& painter, const QWidget& widget, const Wallpaper& wallpaper, double shown)
{
    // The desktop covers the windows from the start, so they show only where they are drawn
    // here until the very end; the dimming comes and goes.
    painter.drawPixmap(widget.rect(), wallpaper.pixmap(widget.size(), widget.devicePixelRatioF()));
    painter.fillRect(widget.rect(), QColor(0, 0, 0, int(120 * shown)));
}

Background::Background(const Wallpaper& wallpaper, QScreen* screen, QWidget* parent)
    : QWidget(parent)
    , m_wallpaper(wallpaper)
{
    setWindowTitle(u"Desktop"_s);
    connect(&wallpaper, &Wallpaper::changed, this, qOverload<>(&QWidget::update));

    shell::coverScreen(*this, LayerShellQt::Window::LayerBackground, false, u"tde-argus-background"_s, screen);
}

void Background::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawPixmap(rect(), m_wallpaper.pixmap(size(), devicePixelRatioF()));
}

} // namespace argus
