#include "AppletButton.hpp"

#include <QMenu>
#include <QPainter>
#include <QWidgetAction>

namespace hermes {
namespace {

// The panel below the bar, which the applet draws in.
class AppletPopup : public QWidget {
public:
    AppletPopup(LuaApplet& applet, QWidget* parent)
        : QWidget(parent)
        , m_applet(applet)
    {
        setFixedSize(*applet.popupSize());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        m_applet.paintPopup(painter, size());
    }

private:
    LuaApplet& m_applet;
};

} // namespace

AppletButton::AppletButton(std::unique_ptr<LuaApplet> applet, QWidget* parent)
    : QToolButton(parent)
    , m_applet(std::move(applet))
{
    setAutoRaise(true);
    setAccessibleName(m_applet->name());
    if (m_applet->popupSize()) {
        auto* menu = new QMenu(this);
        m_popup = new AppletPopup(*m_applet, menu);
        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(m_popup);
        menu->addAction(action);
        setMenu(menu);
        setPopupMode(QToolButton::InstantPopup);
    }
    connect(&m_timer, &QTimer::timeout, this, &AppletButton::tick);
    m_timer.start(m_applet->interval());
    tick();
}

QSize AppletButton::sizeHint() const
{
    return {m_applet->width(), QToolButton::sizeHint().height()};
}

void AppletButton::tick()
{
    m_applet->update();
    update();
    if (m_popup && m_popup->isVisible())
        m_popup->update();
}

void AppletButton::paintEvent(QPaintEvent* event)
{
    // The highlight under the pointer and while the panel is open, then the applet over it.
    QToolButton::paintEvent(event);
    QPainter painter(this);
    m_applet->paint(painter, size());
}

} // namespace hermes
