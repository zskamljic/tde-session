#pragma once

#include <QWidget>

#include <memory>
#include <vector>

namespace hermes {

class StatusNotifierItem;
class Watcher;

// The icons programs keep in the system tray, as status notifier items: a click activates
// one, a right click shows its menu, the wheel scrolls it.
class Tray : public QWidget {
    Q_OBJECT

public:
    explicit Tray(QWidget* parent = nullptr);
    ~Tray() override;

    QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void add(const QString& item);
    void remove(const QString& item);

private:
    std::vector<StatusNotifierItem*> shown() const;
    StatusNotifierItem* itemAt(QPoint pos) const;
    QRect iconRect(int index) const;
    void itemsChanged();

    std::unique_ptr<Watcher> m_watcher;
    std::vector<std::unique_ptr<StatusNotifierItem>> m_items;
    StatusNotifierItem* m_hovered = nullptr;
};

} // namespace hermes
