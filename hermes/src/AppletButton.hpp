#pragma once

#include "LuaApplet.hpp"

#include <QTimer>
#include <QToolButton>

#include <memory>

namespace hermes {

class SystemUsage;

// An applet in the bar, drawn by its Lua file and updated as often as it asks; one with a
// panel opens it below when clicked.
class AppletButton : public QToolButton {
    Q_OBJECT

public:
    AppletButton(std::unique_ptr<LuaApplet> applet, QWidget* parent);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void tick();

    std::unique_ptr<LuaApplet> m_applet;
    QWidget* m_popup = nullptr; // the panel's drawing, while it has one
    QTimer m_timer;
};

} // namespace hermes
