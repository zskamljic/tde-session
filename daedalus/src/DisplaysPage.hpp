#pragma once

#include "Pages.hpp"

#include <Displays.hpp>

#include <QWidget>

#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace daedalus {

// Where displays are next to each other, drawn small, for moving them with the pointer: a
// display let go next to others moves to touch the nearest of them, without overlapping any.
class Arrangement : public QWidget {
    Q_OBJECT

public:
    struct Item {
        QString name;
        QString label {};
        QRect area; // in the layout of all of them
        bool primary = false;
    };

    explicit Arrangement(QWidget* parent = nullptr);

    void setItems(std::vector<Item> items, const QString& selected);
    QSize sizeHint() const override { return {560, 220}; }

    // Where `area` goes next to `others`: the nearest place touching one of them that
    // overlaps none. Exposed for testing.
    static QPoint snap(const QRect& area, const std::vector<QRect>& others);

signals:
    void selected(const QString& name);
    void moved(const QString& name, QPoint position);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // From the layout to this widget: how much smaller, and where its origin is.
    double scale() const;
    QPointF origin() const;
    QRectF onWidget(const QRect& area) const;

    std::vector<Item> m_items;
    QString m_selected;
    std::optional<QPointF> m_grab; // where the dragged display was taken, in the layout
    QRect m_bounds; // of all of them, as they were when the drag began
};

// The displays: how they are arranged, which one has the bar, and the mode, scale and turn of
// each. Changes are tried out first and kept only when the user says so, as a display may not
// show anything with them.
class DisplaysPage : public Page {
    Q_OBJECT

public:
    DisplaysPage(Settings& settings, QWidget* parent = nullptr);

private:
    void load();
    void showSelected();
    void edited();
    void apply();
    void confirm(std::vector<shell::DisplaySetting> previous);
    shell::DisplaySetting* setting(const QString& name);
    const shell::Display* display(const QString& name) const;
    // After a display changed size, it touches the others again without covering them.
    void settle(const QString& name);

    Settings& m_settings;
    shell::Displays m_displays;
    std::vector<shell::DisplaySetting> m_edit; // what the user made of them
    QString m_selected;
    bool m_applying = false;

    Arrangement* m_arrangement = nullptr;
    Group* m_group = nullptr;
    class Switch* m_primary = nullptr;
    class Switch* m_enabled = nullptr;
    QComboBox* m_resolution = nullptr;
    QComboBox* m_refresh = nullptr;
    QComboBox* m_scale = nullptr;
    QComboBox* m_orientation = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_reset = nullptr;
    QLabel* m_unsupported = nullptr;
};

} // namespace daedalus
