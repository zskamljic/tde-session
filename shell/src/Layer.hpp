#pragma once

#include <LayerShellQt/Window>

#include <QScreen>
#include <QString>
#include <QWidget>
#include <QWindow>

namespace shell {

// Puts a layer surface on `screen`, for the next time it shows.
inline void placeOnScreen(QWidget& widget, QScreen* screen)
{
    if (!screen || !widget.windowHandle())
        return;
    // The window knows its screen too, so that what it draws fits it.
    widget.windowHandle()->setScreen(screen);
    if (auto* surface = LayerShellQt::Window::get(widget.windowHandle()))
        surface->setScreen(screen);
}

// Makes `widget` a layer surface covering the whole of `screen`, or the one the compositor
// picks, in `layer`; with `keyboard`, it takes the keyboard while shown.
inline void coverScreen(
    QWidget& widget, LayerShellQt::Window::Layer layer, bool keyboard, const QString& scope, QScreen* screen = nullptr)
{
    using Window = LayerShellQt::Window;
    widget.winId(); // makes the native window, which is what becomes the layer surface
    if (auto* surface = Window::get(widget.windowHandle())) {
        surface->setLayer(layer);
        surface->setAnchors(
            Window::Anchors(Window::AnchorTop | Window::AnchorBottom | Window::AnchorLeft | Window::AnchorRight));
        surface->setExclusiveZone(-1);
        surface->setKeyboardInteractivity(
            keyboard ? Window::KeyboardInteractivityExclusive : Window::KeyboardInteractivityNone);
        surface->setScope(scope);
    }
    placeOnScreen(widget, screen);
}

} // namespace shell
