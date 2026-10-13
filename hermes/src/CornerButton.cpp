#include "CornerButton.hpp"

#include <QPainter>

namespace hermes {
namespace {

constexpr int Width = 12;

} // namespace

CornerButton::CornerButton(QWidget* parent)
    : QAbstractButton(parent)
{
    // Lit while the pointer is on it, as when pushed into the corner.
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus);
}

QSize CornerButton::sizeHint() const
{
    return {Width, 0};
}

void CornerButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    // A pane of glass on the bar, brighter under the pointer and while pressed, with an edge
    // that sets it apart from the applications.
    const int light = isDown() ? 80 : underMouse() ? 55 : 28;
    painter.fillRect(rect(), QColor(255, 255, 255, light));
    painter.fillRect(QRect(width() - 1, 0, 1, height()), QColor(255, 255, 255, 60));
    painter.fillRect(QRect(width() - 2, 0, 1, height()), QColor(0, 0, 0, 60));
}

} // namespace hermes
