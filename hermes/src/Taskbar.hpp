#pragma once

#include "Groups.hpp"

#include <Catalog.hpp>
#include <Windows.hpp>

#include <QWidget>

#include <vector>

namespace hermes {

using shell::Window;
using shell::Windows;

// The running applications as icons, the windows of each grouped on one button. A click
// switches to the application's window; a right click lists its windows.
class Taskbar : public QWidget {
    Q_OBJECT

public:
    explicit Taskbar(Windows& windows, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void rebuild();
    std::vector<OpenWindow> openWindows() const;
    QRect buttonRect(int index) const;
    int indexAt(QPoint pos) const;
    void launch(const shell::Application* app);
    void showMenu(const Group& group, QPoint pos);

    Windows& m_windows;
    shell::Catalog m_catalog;
    std::vector<Group> m_groups;
    int m_hovered = -1;
};

} // namespace hermes
