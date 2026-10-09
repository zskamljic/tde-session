#pragma once

#include <QOpenGLWidget>
#include <QPainter>
#include <QSurfaceFormat>

#include <functional>

namespace argus {

// Where something covering a whole screen, moving with every frame, is drawn: with OpenGL, by
// the graphics card, as the processor alone is too slow for it on large screens. It fills
// the widget it belongs to, under its other children, and leaves the pointer to it. What is
// drawn comes from `paint`, as a paintEvent would draw it, over nothing.
class Canvas : public QOpenGLWidget {
public:
    Canvas(QWidget* parent, std::function<void(QPainter&)> paint)
        : QOpenGLWidget(parent)
        , m_paint(std::move(paint))
    {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setAlphaBufferSize(8);
        format.setSamples(4); // for smooth rounded corners
        setFormat(format);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        lower();
    }

protected:
    void paintGL() override
    {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        m_paint(painter);
    }

private:
    std::function<void(QPainter&)> m_paint;
};

} // namespace argus
