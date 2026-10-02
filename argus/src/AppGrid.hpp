#pragma once

#include <Applications.hpp>

#include <QWidget>

#include <vector>

namespace argus {

using shell::Application;

// Applications as icons with their names, in rows centred across the widget. One is
// selected for the keyboard; a click or Enter starts it.
class AppGrid : public QWidget {
    Q_OBJECT

public:
    explicit AppGrid(QWidget* parent = nullptr);

    // Shows `apps`, scrolled to the top with the first one selected.
    void setApplications(std::vector<const Application*> apps);
    bool isEmpty() const { return m_apps.empty(); }

    // Moves the selection by `columns` across and `rows` down, wrapping at the ends.
    void moveSelection(int columns, int rows);
    void activateSelected();

signals:
    void activated(const Application* app);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int columns() const;
    int rowCount() const;
    int visibleRows() const;
    QRect cellRect(int index) const;
    int indexAt(QPoint pos) const;
    void scrollTo(int row);
    void ensureVisible(int index);

    std::vector<const Application*> m_apps;
    int m_selected = -1;
    int m_hovered = -1;
    int m_firstRow = 0;
    double m_wheel = 0; // scrolling not yet worth a whole row
};

} // namespace argus
