#pragma once

#include <QAbstractButton>
#include <QScrollArea>
#include <QWidget>
#include <Tween.hpp>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;

// The parts settings pages are made of, after GNOME's: groups of rows in rounded boxes, in a
// column of limited width.
namespace daedalus {

// An on and off switch.
class Switch : public QAbstractButton {
    Q_OBJECT

public:
    explicit Switch(QWidget* parent = nullptr);

    QSize sizeHint() const override { return {46, 26}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    shell::Tween m_knob; // 0 off, 1 on
};

// A row of a group: what it is about on the left, with a line of explanation below, and what
// changes it on the right. A click anywhere on a row with a switch flips it.
class Row : public QWidget {
public:
    Row(const QString& title, const QString& subtitle = {}, QWidget* control = nullptr, QWidget* parent = nullptr);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QWidget* m_control;
};

// Rows in a rounded box, with lines between them, and a title above.
class Group : public QWidget {
public:
    explicit Group(const QString& title = {}, QWidget* parent = nullptr);

    Row* addRow(const QString& title, const QString& subtitle = {}, QWidget* control = nullptr);
    // Something else than rows, below the box, such as an explanation.
    void addNote(const QString& text);
    void setTitle(const QString& title);
    // A button at the right end of the title.
    void setHeaderWidget(QWidget* widget);
    // For contents that are not rows, without the box.
    QVBoxLayout* layout() const { return m_layout; }

private:
    QLabel* m_title = nullptr;
    QHBoxLayout* m_header = nullptr;
    QWidget* m_box = nullptr;
    QVBoxLayout* m_rows = nullptr;
    QVBoxLayout* m_layout = nullptr;
};

// A page: its groups one below the other, in a column in the middle, scrolling when long.
class Page : public QScrollArea {
public:
    explicit Page(QWidget* parent = nullptr);

    Group* addGroup(const QString& title = {});
    void addWidget(QWidget* widget, Qt::Alignment alignment = {});

private:
    QVBoxLayout* m_column = nullptr;
};

} // namespace daedalus
