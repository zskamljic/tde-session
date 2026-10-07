#include "Picker.hpp"

#include <Layer.hpp>

#include <QKeyEvent>

#include <algorithm>

namespace argus {
namespace {

constexpr int EarlyRelease = 1000; // ms a release may come before its request

} // namespace

Picker::Picker(Toplevels& toplevels, const QString& scope, QWidget* parent)
    : QWidget(parent)
    , m_toplevels(toplevels)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    shell::coverScreen(*this, LayerShellQt::Window::LayerOverlay, true, scope);
}

void Picker::pick(bool backwards, bool sameApplication)
{
    const int by = backwards ? -1 : 1;
    // Asked again while closing, as a quick second Super+Tab does: it opens anew.
    if (m_closing)
        finish();
    if (isVisible()) {
        move(by);
        return;
    }
    m_pendingMoves += by;
    if (m_opening)
        return;
    m_opening = true;
    // A release that overtook this request on the bus belongs to it.
    m_released = m_releasedEarly.isValid() && m_releasedEarly.elapsed() < EarlyRelease;
    m_releasedEarly.invalidate();
    m_sameApplication = sameApplication;
    m_toplevels.refresh(this, [this] { appear(); });
}

void Picker::appear()
{
    m_opening = false;
    const int moves = std::exchange(m_pendingMoves, 0);

    std::vector<const Toplevel*> windows;
    for (const auto& window : m_toplevels.windows())
        windows.push_back(window.get());
    // Those the compositor did not place go last, in the order they opened.
    std::ranges::stable_sort(windows, {}, &Toplevel::depth);
    if (m_sameApplication && !windows.empty()) {
        const QString appId = windows.front()->appId;
        std::erase_if(windows, [&](const Toplevel* window) { return window->appId != appId; });
    }
    m_order.clear();
    for (const Toplevel* window : windows)
        m_order.push_back(window->id);
    if (m_order.empty())
        return;

    m_moves = 0;
    // Let go of already, as a quick tap does: the window is switched to without showing any.
    if (m_released) {
        move(moves);
        if (picked() > 0)
            m_toplevels.activate(m_order[size_t(picked())]);
        return;
    }
    shell::placeOnScreen(*this, m_screen);
    setKeyboard(true);
    show();
    setFocus();
    opened();
    move(moves);
}

void Picker::release()
{
    if (m_opening)
        m_released = true;
    else if (isVisible())
        choose(picked());
    else
        m_releasedEarly.start();
}

void Picker::move(int by)
{
    if (count() < 2 || by == 0 || m_closing)
        return;
    m_moves += by;
    moved();
}

void Picker::choose(int index)
{
    if (m_closing || m_order.empty())
        return;
    m_closing = true;
    // Closing, the keys go back to the compositor, where they can open it again.
    setKeyboard(false);
    m_chosen = m_order[size_t(std::max(index, 0))];
    // It comes forward behind this, which closes onto it.
    if (index >= 0)
        m_toplevels.activate(m_chosen);
    closing();
}

void Picker::setKeyboard(bool taken)
{
    if (auto* layer = LayerShellQt::Window::get(windowHandle())) {
        layer->setKeyboardInteractivity(taken ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                              : LayerShellQt::Window::KeyboardInteractivityNone);
    }
}

void Picker::finish()
{
    m_closing = false;
    hide();
}

const Toplevel* Picker::window(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_toplevels.find(m_order[size_t(index)]);
}

int Picker::picked() const
{
    return count() == 0 ? -1 : ((m_moves % count()) + count()) % count();
}

void Picker::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Tab:
    case Qt::Key_QuoteLeft: // the key above Tab, for the windows of one application
    case Qt::Key_Right:
    case Qt::Key_Down:
        move(1);
        break;
    case Qt::Key_Backtab:
    case Qt::Key_AsciiTilde:
    case Qt::Key_Left:
    case Qt::Key_Up:
        move(-1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        choose(picked());
        break;
    case Qt::Key_Escape:
        choose(-1);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace argus
